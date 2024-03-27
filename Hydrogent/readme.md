# Hydrogent

Hydrogent is an implementation of the Hydra rendering API in Diligent Engine.

## Build Instructions

### Windows

1. Download and build [OpenUSD](https://github.com/PixarAnimationStudios/OpenUSD)
    *  If using Visual Studio, build each configuration in a respective folder, for example:
        ```
        python .\build_scripts\build_usd.py --build-variant debug .\build\Debug
        python .\build_scripts\build_usd.py --build-variant release .\build\Release
        python .\build_scripts\build_usd.py --build-variant relwithdebuginfo .\build\RelWithDebInfo
        ```

2. Provide a path to the USD install folder with `DILIGENT_USD_PATH` CMake variable, for example:
   `-DDILIGENT_USD_PATH=c:\GitHub\OpenUSD\build`

3. Build the engine by following [standard instructions](https://github.com/DiligentGraphics/DiligentEngine#build-and-run-instructions)

### MacOS

1. Install [Python 3.12](https://www.python.org/downloads/) from the official site

2. Install [OpenUSD](https://github.com/PixarAnimationStudios/OpenUSD)
    *  Open `Terminal` in the directory where you want to install [OpenUSD](https://github.com/PixarAnimationStudios/OpenUSD) for example `/Users/${USER_NAME}/Desktop/Projects`
    *  Modify `PATH` to installed the Python
        ```bash
        export PATH=/Library/Frameworks/Python.framework/Versions/3.12/bin:$PATH
        ```
    *  Install dependencies for [OpenUSD](https://github.com/PixarAnimationStudios/OpenUSD)
        ```bash
        sudo pip3 install PySide6
        sudo pip3 install PyOpenGL
        sudo pip3 install setuptools       
        ```
    *  Download and build [OpenUSD](https://github.com/PixarAnimationStudios/OpenUSD)
        ```bash
        git clone https://github.com/PixarAnimationStudios/OpenUSD.git
        cd OpenUSD
        python3 ./build_scripts/build_usd.py --build-variant debug --no-materialx --generator Xcode  ./build -v
        ```

3. Set environment variables 
    *  Create `.zsrsh` file at path `/Users/${USER_NAME}/` if itsn't created
    *  Paste environment variables to this file
        ```bash
        export OPEN_USD_BUILD=/Users/${USER_NAME}/Desktop/Projects/OpenUSD/build
        export PYTHONPATH=/Users/${USER_NAME}/Desktop/Projects/OpenUSD/build/lib/python
        export PYTHON_3_12=/Library/Frameworks/Python.framework/Versions/3.12

        export PATH=$PYTHON_3_12/bin:$PATH
        export PATH=$VULKAN_SDK/bin:$PATH
        export PATH=$OPEN_USD_BUILD/bin:$PATH
        ```

4. Provide a path to the USD install folder with `DILIGENT_USD_PATH` CMake variable, for example:
   `-DDILIGENT_USD_PATH=/Users/${USER_NAME}/Desktop/Projects/OpenUSD/build`

5. Build the engine by following [standard instructions](https://github.com/DiligentGraphics/DiligentEngine#build-and-run-instructions)

## Run Instructions

To run an application that uses Hydrogent on Windows, the following paths must be added to the `PATH` environment variable for
the application to find required USD DLLs:

```
${DILIGENT_USD_PATH}/lib
${DILIGENT_USD_PATH}/bin
```
