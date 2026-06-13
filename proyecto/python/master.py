#!/usr/bin/env python3
"""
╔═══════════════════════════════════════════════════════════════════════════╗
║  MAESTRO  –  Coordinador del aprendizaje federado (fila a fila)           ║
║                                                                            ║
║  Flujo por MOMENTO i (= fila i del dataset):                              ║
║    1. Maestro hace forward + backpropagation con su fila i                ║
║    2. Extrae la matriz de pesos actualizada (POST-backprop)               ║
║    3. Envía la MISMA matriz a todos los esclavos    → TYPE 'M'            ║
║    4. Recibe la matriz actualizada de cada esclavo  ← TYPE 'm'           ║
║    5. Calcula la MEDIA de todas las matrices                              ║
║       (maestro + esclavos)                                                ║
║    6. Actualiza el MLP con la media → siguiente momento                  ║
║                                                                            ║
║  Antes del loop: envía la porción del dataset a cada esclavo → TYPE 'D'  ║
║                                                                            ║
║  Los datagramas se muestran en terminal (C++) con:                        ║
║    WRITE:>>>[CS][ID][FL][SQ][T][DS][DATA...]<<<                           ║
║    READ :>>>[CS][ID][FL][SQ][T][DS][DATA...]<<<                           ║
╚═══════════════════════════════════════════════════════════════════════════╝
"""

import argparse
import sys
import numpy as np
import torch
import torch.nn as nn
import torch.nn.functional as F
from torch.utils.data import DataLoader, TensorDataset
import torch.optim as optim
import pandas as pd
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
from sklearn.metrics import (confusion_matrix, ConfusionMatrixDisplay,
                             classification_report)

try:
    import comm_module
except ImportError:
    print("ERROR: comm_module no encontrado.")
    print("  cd cpp && python setup.py build_ext --inplace")
    print("  cp comm_module*.so ../python/")
    sys.exit(1)

# ══════════════════════════════════════════════════════════════════════════
#  Configuración
# ══════════════════════════════════════════════════════════════════════════
MASTER_PORT     = 9000
SLAVE_PORT_BASE = 9001   # esclavo i → puerto (9001 + i - 1), ej: i=4 → 9004
SLAVE_HOST      = "127.0.0.1"

INPUT_DIM       = 14
NUM_CLASSES     = 3
HIDDEN1         = 128
HIDDEN2         = 64
LR              = 0.001

RECV_TIMEOUT_MS = 120_000   # 2 min por esclavo


# ══════════════════════════════════════════════════════════════════════════
#  Modelo  (idéntico en maestro y esclavos)
# ══════════════════════════════════════════════════════════════════════════
class MulticlassClassifier(nn.Module):
    def __init__(self):
        super().__init__()
        self.fc1            = nn.Linear(INPUT_DIM, HIDDEN1)
        self.fc2            = nn.Linear(HIDDEN1, HIDDEN2)
        self.class_logits   = nn.Linear(HIDDEN2, NUM_CLASSES)
        self.class_log_vars = nn.Linear(HIDDEN2, NUM_CLASSES)

    def forward(self, x):
        x = F.relu(self.fc1(x))
        x = F.relu(self.fc2(x))
        return self.class_logits(x), self.class_log_vars(x)


# ══════════════════════════════════════════════════════════════════════════
#  Serialización de pesos
# ══════════════════════════════════════════════════════════════════════════
def weights_to_bytes(model: nn.Module) -> bytes:
    """Aplana todos los parámetros en orden → bytes float32."""
    return np.concatenate([
        p.data.cpu().numpy().astype(np.float32).flatten()
        for p in model.parameters()
    ]).tobytes()


def bytes_to_weights(model: nn.Module, data: bytes) -> None:
    """Carga bytes float32 en los parámetros del modelo (in-place)."""
    flat = np.frombuffer(data, dtype=np.float32).copy()
    ptr  = 0
    with torch.no_grad():
        for param in model.parameters():
            n = param.numel()
            param.data.copy_(
                torch.from_numpy(flat[ptr:ptr + n].reshape(param.shape)))
            ptr += n


def mean_weights(weight_bytes_list: list) -> bytes:
    """Media elemento a elemento de una lista de matrices de pesos (bytes)."""
    arrays = np.stack([np.frombuffer(b, dtype=np.float32)
                       for b in weight_bytes_list])
    return arrays.mean(axis=0).astype(np.float32).tobytes()


# ══════════════════════════════════════════════════════════════════════════
#  Serialización del dataset para enviarlo por UDP
# ══════════════════════════════════════════════════════════════════════════
def serialize_dataset(X: np.ndarray, y: np.ndarray) -> bytes:
    """
    Header 12 bytes: [n_samples:uint32][n_features:uint32][n_classes:uint32]
    Luego: X flat float32  |  y flat float32
    """
    n, f = X.shape
    _, c = y.shape
    hdr = np.array([n, f, c], dtype=np.uint32)
    return (hdr.tobytes()
            + X.astype(np.float32).tobytes()
            + y.astype(np.float32).tobytes())


# ══════════════════════════════════════════════════════════════════════════
#  Carga y partición del dataset
# ══════════════════════════════════════════════════════════════════════════
def load_and_partition(csv_path: str, n_slaves: int):
    """
    Carga el CSV y divide en (n_slaves + 1) partes IGUALES.
    Parte 0 → maestro  |  Partes 1..N → esclavos
    Retorna: (X_master, y_master), [(X_s1,y_s1), (X_s2,y_s2), ...]
    """
    df   = pd.read_csv(csv_path, header=None, skiprows=1)
    X_np = df.iloc[:, :INPUT_DIM].values.astype(np.float32)
    y_np = df.iloc[:, -NUM_CLASSES:].values.astype(np.float32)

    total   = len(X_np)
    n_parts = n_slaves + 1
    size    = total // n_parts          # tamaño base por partición
    # Usar exactamente size filas por partición (descartar el residuo)
    parts_X = [X_np[i * size:(i + 1) * size] for i in range(n_parts)]
    parts_y = [y_np[i * size:(i + 1) * size] for i in range(n_parts)]

    return (parts_X[0], parts_y[0]), list(zip(parts_X[1:], parts_y[1:]))


# ══════════════════════════════════════════════════════════════════════════
#  Función principal
# ══════════════════════════════════════════════════════════════════════════
def main():
    parser = argparse.ArgumentParser(description="Maestro – Federated Learning")
    parser.add_argument("--csv",    default="Dataset of Diabetes .csv")
    parser.add_argument("--slaves",    type=int, default=3, help="Número de esclavos")
    parser.add_argument("--no-slaves", action="store_true",
                        help="Modo solo maestro: usa todo el dataset sin esclavos ni red")
    args    = parser.parse_args()
    if args.no_slaves:
        n_slaves = 0    # load_and_partition con 0 esclavos devuelve todo el CSV al maestro
        s_ports  = []
    else:
        n_slaves = args.slaves
        s_ports  = [SLAVE_PORT_BASE + i for i in range(n_slaves)]

    sep = "=" * 50
    print(sep)
    print("  MAESTRO – Aprendizaje Federado (batch=1)")
    print(sep)
    print(f"  Puerto maestro  : {MASTER_PORT}")
    if args.no_slaves:
        print("  Modo             : Solo maestro — dataset completo, sin red")
    else:
        print(f"  Puertos esclavos: {s_ports}")
    print()

    # En modo --no-slaves no se usa red, node queda en None
    node = None if args.no_slaves else comm_module.RDTNode(MASTER_PORT, 0, print)

    # ── Cargar y particionar dataset ──────────────────────────────────────
    (X_m, y_m), slave_parts = load_and_partition(args.csv, n_slaves)
    n_momentos = len(X_m)   # número de filas = número de momentos

    print(f"  Momentos totales: {n_momentos} (una fila por momento)")
    print(f"  Datos maestro   : {len(X_m)} filas")
    for i, (Xs, _) in enumerate(slave_parts, 1):
        print(f"  Datos esclavo {i} : {len(Xs)} filas")
    print()

    # ── Modelo y optimizador ──────────────────────────────────────────────
    model     = MulticlassClassifier()
    criterion = nn.CrossEntropyLoss()
    optimizer = optim.Adam(model.parameters(), lr=LR)

    # ── Convertir datos del maestro a tensores (fila a fila) ──────────────
    X_t = torch.tensor(X_m)   # shape [n_momentos, INPUT_DIM]
    y_t = torch.tensor(y_m)   # shape [n_momentos, NUM_CLASSES]

    # ══════════════════════════════════════════════════════════════════════
    #  DISTRIBUCIÓN DEL DATASET A LOS ESCLAVOS (solo una vez, antes del loop)
    # ══════════════════════════════════════════════════════════════════════
    if not args.no_slaves:
        print(sep)
        print("  DISTRIBUCIÓN DEL DATASET")
        print(sep)
        for i, ((Xs, ys), port) in enumerate(zip(slave_parts, s_ports), 1):
            data_bytes = serialize_dataset(Xs, ys)
            print(f"\n[Maestro] → Enviando dataset a Esclavo {i} "
                  f"({len(Xs)} filas, {len(data_bytes)} bytes)...")
            try:
                node.send_data(SLAVE_HOST, port, i, data_bytes)
                print(f"[Maestro] ✓ Dataset enviado a Esclavo {i}")
            except RuntimeError as e:
                print(f"[Maestro] ⚠ Error enviando dataset a Esclavo {i}: {e}")
        print()

    # ══════════════════════════════════════════════════════════════════════
    #  MÉTRICAS
    # ══════════════════════════════════════════════════════════════════════
    losses        = []    # pérdida del maestro por momento
    y_true_all    = []
    y_pred_all    = []

    # ══════════════════════════════════════════════════════════════════════
    #  LOOP PRINCIPAL: un MOMENTO por fila
    # ══════════════════════════════════════════════════════════════════════
    for momento in range(n_momentos):
        print(sep)
        print(f"  MOMENTO {momento + 1} / {n_momentos}")
        print(sep)

        # ── PASO 1: Maestro entrena con la fila `momento` (batch = 1) ────
        sx = X_t[momento].unsqueeze(0)   # shape [1, INPUT_DIM]
        sy = y_t[momento].unsqueeze(0)   # shape [1, NUM_CLASSES]

        model.train()
        optimizer.zero_grad()
        logits, _ = model(sx)
        loss = criterion(logits, sy)
        loss.backward()           # ← BACKPROPAGATION
        optimizer.step()
        losses.append(loss.item())

        pred  = logits.argmax(dim=1).item()
        label = sy.argmax(dim=1).item()
        y_true_all.append(label)
        y_pred_all.append(pred)

        print(f"[Maestro] Momento {momento+1}: loss={loss.item():.4f} "
              f"pred={pred} label={label}")

        # ── PASO 2: Extraer pesos POST-backprop ───────────────────────────
        master_w = weights_to_bytes(model)
        all_weights = [master_w]   # la del maestro siempre se incluye

        # ── PASO 3-4: Para cada esclavo: enviar pesos, recibir pesos ──────
        for i, port in enumerate(s_ports, 1):
            print(f"\n[Maestro] → Enviando pesos (TYPE=M) al Esclavo {i}...")
            try:
                node.send_matrix(SLAVE_HOST, port, i, master_w)
                print(f"[Maestro] ✓ Pesos enviados al Esclavo {i}")
            except RuntimeError as e:
                print(f"[Maestro] ⚠ Error enviando a Esclavo {i}: {e}")
                continue

            print(f"[Maestro] ← Esperando pesos (TYPE=m) del Esclavo {i}...")
            result = node.recv_any(RECV_TIMEOUT_MS)
            if result is None:
                print(f"[Maestro] ⚠ Timeout Esclavo {i} — se omite")
                continue
            tipo, raw = result
            if tipo != 'm':
                print(f"[Maestro] ⚠ Tipo inesperado '{tipo}' del Esclavo {i}")
                continue
            all_weights.append(raw)
            print(f"[Maestro] ✓ Pesos recibidos del Esclavo {i} ({len(raw)} B)")

        # ── PASO 5: Media de todas las matrices ───────────────────────────
        avg_bytes = mean_weights(all_weights)
        n_contrib = len(all_weights)

        # ── PASO 6: Actualizar MLP con la media ───────────────────────────
        bytes_to_weights(model, avg_bytes)
        print(f"\n[Maestro] Media calculada con {n_contrib} matrices → "
              f"modelo actualizado")

    # ══════════════════════════════════════════════════════════════════════
    #  RESULTADOS FINALES
    # ══════════════════════════════════════════════════════════════════════
    correct = sum(t == p for t, p in zip(y_true_all, y_pred_all))
    acc     = correct / max(len(y_true_all), 1)

    print("\n" + "=" * 50)
    print("  RESULTADOS FINALES DEL MAESTRO")
    print("=" * 50)
    print(f"  Momentos procesados : {n_momentos}")
    print(f"  Loss final          : {losses[-1]:.4f}")
    print(f"  Loss promedio       : {sum(losses)/len(losses):.4f}")
    print(f"  Accuracy (local)    : {acc:.4f}  ({correct}/{len(y_true_all)})")
    print()
    print("Classification Report:")
    print(classification_report(y_true_all, y_pred_all, digits=3,
                                 zero_division=0))

    # ── Gráficas ──────────────────────────────────────────────────────────
    fig, axes = plt.subplots(1, 2, figsize=(12, 4))

    axes[0].plot(range(1, n_momentos + 1), losses, linewidth=0.8)
    axes[0].set_xlabel("Momento")
    axes[0].set_ylabel("Loss")
    axes[0].set_title("Federated Learning – Loss del Maestro (batch=1)")
    axes[0].grid(True)

    cm   = confusion_matrix(y_true_all, y_pred_all)
    disp = ConfusionMatrixDisplay(cm,
                                  display_labels=[f"C{i}"
                                                  for i in range(NUM_CLASSES)])
    disp.plot(cmap=plt.cm.Blues, ax=axes[1])
    axes[1].set_title("Matriz de Confusión (todos los momentos)")

    plt.tight_layout()
    plt.savefig("master_resultados.png", dpi=120)
    print("[Maestro] Gráficas guardadas → master_resultados.png")


if __name__ == "__main__":
    main()
