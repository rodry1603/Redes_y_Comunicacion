#!/usr/bin/env python3
"""
╔═══════════════════════════════════════════════════════════════════════════╗
║  ESCLAVO  –  Nodo de entrenamiento local (fila a fila)                    ║
║                                                                            ║
║  Flujo:                                                                    ║
║    [Una sola vez al inicio]                                               ║
║      1. Recibe su porción del dataset del maestro  ← TYPE 'D'            ║
║                                                                            ║
║    [Por cada MOMENTO i]                                                   ║
║      2. Recibe la matriz de pesos del maestro      ← TYPE 'M'            ║
║      3. Carga los pesos en su modelo local                                ║
║      4. Entrena con su FILA i (forward + backprop) batch = 1             ║
║      5. Envía la matriz de pesos actualizada       → TYPE 'm'            ║
║                                                                            ║
║  Uso (una terminal por esclavo):                                          ║
║    python slave.py --node-id 1 --port 9001                               ║
║    python slave.py --node-id 2 --port 9002                               ║
║    python slave.py --node-id 3 --port 9003                               ║
╚═══════════════════════════════════════════════════════════════════════════╝
"""

import argparse
import sys
import numpy as np
import torch
import torch.nn as nn
import torch.nn.functional as F
import torch.optim as optim

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
MASTER_HOST     = "127.0.0.1"
MASTER_PORT     = 9000

INPUT_DIM       = 14
NUM_CLASSES     = 3
HIDDEN1         = 128
HIDDEN2         = 64
LR              = 0.001

RECV_TIMEOUT_MS = 120_000   # 2 min esperando al maestro


# ══════════════════════════════════════════════════════════════════════════
#  Modelo  (idéntico al del maestro)
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
    return np.concatenate([
        p.data.cpu().numpy().astype(np.float32).flatten()
        for p in model.parameters()
    ]).tobytes()


def bytes_to_weights(model: nn.Module, data: bytes) -> None:
    flat = np.frombuffer(data, dtype=np.float32).copy()
    ptr  = 0
    with torch.no_grad():
        for param in model.parameters():
            n = param.numel()
            param.data.copy_(
                torch.from_numpy(flat[ptr:ptr + n].reshape(param.shape)))
            ptr += n


# ══════════════════════════════════════════════════════════════════════════
#  Deserialización del dataset recibido por UDP
#  Formato: [n:u32][f:u32][c:u32][X float32 flat][y float32 flat]
# ══════════════════════════════════════════════════════════════════════════
def deserialize_dataset(data: bytes):
    """
    Retorna (X, y) como numpy arrays float32.
    X.shape = [n_samples, n_features]
    y.shape = [n_samples, n_classes]
    """
    hdr     = np.frombuffer(data[:12], dtype=np.uint32)
    n, f, c = int(hdr[0]), int(hdr[1]), int(hdr[2])

    off_x = 12
    off_y = off_x + n * f * 4
    X = np.frombuffer(data[off_x:off_y],
                      dtype=np.float32).reshape(n, f).copy()
    y = np.frombuffer(data[off_y:off_y + n * c * 4],
                      dtype=np.float32).reshape(n, c).copy()
    return X, y


# ══════════════════════════════════════════════════════════════════════════
#  Función principal
# ══════════════════════════════════════════════════════════════════════════
def main():
    parser = argparse.ArgumentParser(description="Esclavo – Federated Learning")
    parser.add_argument("--node-id",    type=int, required=True,
                        help="ID de este esclavo (1, 2, 3 …)")
    parser.add_argument("--port",       type=int, required=True,
                        help="Puerto UDP de escucha")
    parser.add_argument("--max-rounds", type=int, default=10000,
                        help="Máximo de rondas (momentos)")
    args    = parser.parse_args()
    nid     = args.node_id
    port    = args.port

    sep = "=" * 50
    print(sep)
    print(f"  ESCLAVO {nid} – Aprendizaje Federado (batch=1)")
    print(sep)
    print(f"  Puerto escucha : {port}  (node_id={nid})")
    print(f"  Maestro        : {MASTER_HOST}:{MASTER_PORT}")
    print()

    # ── Nodo C++: log_fn = print → datagramas aparecen en terminal ────────
    node = comm_module.RDTNode(port, nid, print)

    model     = MulticlassClassifier()
    criterion = nn.CrossEntropyLoss()
    optimizer = optim.Adam(model.parameters(), lr=LR)

    # ══════════════════════════════════════════════════════════════════════
    #  PASO INICIAL: recibir porción del dataset
    # ══════════════════════════════════════════════════════════════════════
    print(sep)
    print(f"  [Esclavo {nid}] Esperando dataset del maestro (TYPE=D)...")
    print(sep)

    result = node.recv_any(RECV_TIMEOUT_MS)
    if result is None:
        print(f"[Esclavo {nid}] ERROR: Timeout esperando dataset. Abortando.")
        return

    tipo, raw = result
    if tipo != 'D':
        print(f"[Esclavo {nid}] ERROR: Esperaba TYPE='D', llegó '{tipo}'. Abortando.")
        return

    X_local, y_local = deserialize_dataset(raw)
    n_momentos = len(X_local)
    print(f"[Esclavo {nid}] ✓ Dataset recibido: {n_momentos} filas "
          f"({X_local.shape[1]} features, {y_local.shape[1]} clases)")

    # Convertir a tensores
    X_t = torch.tensor(X_local)   # [n_momentos, INPUT_DIM]
    y_t = torch.tensor(y_local)   # [n_momentos, NUM_CLASSES]

    # ══════════════════════════════════════════════════════════════════════
    #  LOOP: un MOMENTO por fila, sincronizado con el maestro
    # ══════════════════════════════════════════════════════════════════════
    for momento in range(min(n_momentos, args.max_rounds)):
        print(f"\n{sep}")
        print(f"  [Esclavo {nid}] MOMENTO {momento + 1} / {n_momentos}")
        print(sep)

        # ── PASO 2: Recibir matriz de pesos del maestro (TYPE = 'M') ──────
        print(f"[Esclavo {nid}] ← Esperando pesos (TYPE=M) del maestro...")
        result = node.recv_any(RECV_TIMEOUT_MS)

        if result is None:
            print(f"[Esclavo {nid}] Timeout — fin del entrenamiento")
            break

        tipo, raw = result
        if tipo != 'M':
            print(f"[Esclavo {nid}] ERROR: Esperaba TYPE='M', llegó '{tipo}'")
            break

        # ── PASO 3: Cargar pesos en el modelo local ───────────────────────
        bytes_to_weights(model, raw)
        print(f"[Esclavo {nid}] ✓ Pesos del maestro cargados ({len(raw)} B)")

        # ── PASO 4: Entrenar con la FILA `momento` (batch = 1) ────────────
        sx = X_t[momento].unsqueeze(0)   # shape [1, INPUT_DIM]
        sy = y_t[momento].unsqueeze(0)   # shape [1, NUM_CLASSES]

        model.train()
        optimizer.zero_grad()
        logits, _ = model(sx)
        loss = criterion(logits, sy)
        loss.backward()           # ← BACKPROPAGATION
        optimizer.step()

        pred  = logits.argmax(dim=1).item()
        label = sy.argmax(dim=1).item()
        print(f"[Esclavo {nid}] Entrenamiento fila {momento+1}: "
              f"loss={loss.item():.4f}  pred={pred}  label={label}")

        # ── PASO 5: Enviar pesos actualizados al maestro (TYPE = 'm') ─────
        updated = weights_to_bytes(model)
        print(f"[Esclavo {nid}] → Enviando pesos (TYPE=m) al maestro "
              f"({len(updated)} B)...")
        try:
            node.send_matrix_slave(MASTER_HOST, MASTER_PORT, 0, updated)
            print(f"[Esclavo {nid}] ✓ Pesos enviados al maestro")
        except RuntimeError as e:
            print(f"[Esclavo {nid}] ⚠ Error enviando: {e}")

    print(f"\n[Esclavo {nid}] Entrenamiento completado "
          f"({min(n_momentos, args.max_rounds)} momentos).")


if __name__ == "__main__":
    main()
