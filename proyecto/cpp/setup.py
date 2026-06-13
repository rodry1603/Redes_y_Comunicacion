"""
Build script para comm_module (RDT sobre UDP).

Uso:
    pip install pybind11
    python setup.py build_ext --inplace

Genera: comm_module.cpython-<ver>-linux-gnu.so (o .pyd en Windows)
El .so se copia automáticamente junto a master.py y slave.py.
"""

from setuptools import setup, Extension
import pybind11

sources = [
    "udp_socket.cpp",
    "rdt.cpp",
    "bindings.cpp",
]

ext = Extension(
    name="comm_module",
    sources=sources,
    include_dirs=[pybind11.get_include()],
    language="c++",
    extra_compile_args=[
        "-std=c++17",
        "-O2",
        "-Wall",
        "-Wextra",
        "-fvisibility=hidden",   # requerido por pybind11
    ],
)

setup(
    name="comm_module",
    version="1.0.0",
    description="Módulo RDT/UDP para aprendizaje federado",
    ext_modules=[ext],
    install_requires=["pybind11>=2.10.0"],
    python_requires=">=3.8",
)
