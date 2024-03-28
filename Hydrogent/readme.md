# Hydrogent

Hydrogent is an implementation of the Hydra rendering API in Diligent Engine.

## Build Instructions

1. Download and build [OpenUSD](https://github.com/PixarAnimationStudios/OpenUSD)
   
   * Make sure all [required dependencies](https://github.com/PixarAnimationStudios/OpenUSD?tab=readme-ov-file#dependencies) are installed,
     in particular, the following python packages:
     * `PySide6`
     * `PyOpenGL`
   
   * Clone the repository
     ```bash
     git clone https://github.com/PixarAnimationStudios/OpenUSD.git
     cd OpenUSD
     ```
   
   * On **Windows**, use the Visual Studio generator and build each configuration in a respective folder, for example:
      ```bash
      python .\build_scripts\build_usd.py --build-variant debug .\build\Debug
      python .\build_scripts\build_usd.py --build-variant release .\build\Release
      python .\build_scripts\build_usd.py --build-variant relwithdebuginfo .\build\RelWithDebInfo
      ```
   
   * On **MacOS**, use the Xcode generator and build each configuration in a respective folder, for example:
      ```bash
      python3 ./build_scripts/build_usd.py --build-variant debug --generator Xcode  ./build/Debug
      python3 ./build_scripts/build_usd.py --build-variant release --generator Xcode  ./build/Release
      python3 ./build_scripts/build_usd.py --build-variant relwithdebuginfo --generator Xcode  ./build/RelWithDebInfo
      ```

2. Provide the path to the USD build folder with `DILIGENT_USD_PATH` CMake variable, for example:
   `-DDILIGENT_USD_PATH=c:\GitHub\OpenUSD\build`

3. Build the engine by following [standard instructions](https://github.com/DiligentGraphics/DiligentEngine#build-and-run-instructions)

## Run Instructions

To run an application that uses Hydrogent on Windows, the following paths must be added to the `PATH` environment variable for
the application to find required USD DLLs:

```
${DILIGENT_USD_PATH}/lib
${DILIGENT_USD_PATH}/bin
```
