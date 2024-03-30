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
   
   * On **Windows**, use the Visual Studio generator and build each configuration in a respective folder.
     Start a developer command prompt for Visual Studio and run the following commands:
      ```bash
      python .\build_scripts\build_usd.py --build-variant debug .\build\Debug
      python .\build_scripts\build_usd.py --build-variant release .\build\Release
      python .\build_scripts\build_usd.py --build-variant relwithdebuginfo .\build\RelWithDebInfo
      ```
     * :warning: Use new shell for each configuration to avoid issues with environment variables.

   * On **MacOS**, use the Xcode generator and build each configuration in a respective folder, for example:
      ```bash
      python3 ./build_scripts/build_usd.py --build-variant debug --generator Xcode  ./build/Debug
      python3 ./build_scripts/build_usd.py --build-variant release --generator Xcode  ./build/Release
      python3 ./build_scripts/build_usd.py --build-variant relwithdebuginfo --generator Xcode  ./build/RelWithDebInfo
      ```

2. Optionally, download and build [USD File Format Plugins](https://github.com/adobe/USD-Fileformat-plugins) to
   support additional file formats (fbx, gltf, obj, etc.):

   * Use the following options `--draco --openimageio` to build OpenUSD with Draco and OpenImageIO support in step 1.
    
     * :warning: if you arleady built OpenUSD, you will need to delete the build folder and start over
       as OpenUSD does not handle changing build options well.

   * On Windows and MacOS, follow the [build instructions](https://github.com/adobe/USD-Fileformat-plugins?tab=readme-ov-file#build),
     but build each configuration in a respective folder. For example, on Windows:
	 ```bash
     cmake -S . -B build -DCMAKE_INSTALL_PREFIX=bin/Debug -Dpxr_ROOT=<OPEN_USD_ROOT>/build/Debug <OPTIONS>
     cmake --build   build/Debug --config debug
     cmake --install build/Debug --config debug
	 ```

3. Provide the path to the USD build folder with `DILIGENT_USD_PATH` CMake variable, for example:
   `-DDILIGENT_USD_PATH=<OPEN_USD_ROOT>\build`

4. Build the engine by following [standard instructions](https://github.com/DiligentGraphics/DiligentEngine#build-and-run-instructions)


## Run Instructions

To run an application that uses Hydrogent on Windows, the following paths must be added to the `PATH` environment variable for
the application to find required USD DLLs. For example, for Debug configuration:

```
<OPEN_USD_ROOT>\build\Debug\lib
<OPEN_USD_ROOT>\build\Debug\bin
```

If you are using USD File Format Plugins, the following paths must be also added to the `PATH` environment variable:

```
<USD_FILEFORMAT_PLUGINS_ROOT>\bin\Debug\lib
<USD_FILEFORMAT_PLUGINS_ROOT>\bin\Debug\plugin\usd
```

Additionally, the following path must be added to the `PXR_PLUGINPATH_NAME` environment variable:

```
<USD_FILEFORMAT_PLUGINS_ROOT>\USD-Fileformat-plugins\bin\Debug\plugin\usd
```
