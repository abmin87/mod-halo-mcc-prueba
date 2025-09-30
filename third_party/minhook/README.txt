MinHook integration placeholder
---------------------------------
Coloca aquí los archivos de MinHook:

1) Incluye (recomendado):
   third_party/minhook/include/MinHook.h

2) Librería x64 (requerido para link):
   third_party/minhook/lib/x64/minhook.lib

Opciones para obtener MinHook:
- vcpkg: vcpkg install minhook:x64-windows
  Copia MinHook.h y minhook.lib a las rutas anteriores.

- Compilación manual desde el repo oficial (MinHook es BSD-2-Clause).
