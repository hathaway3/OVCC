#!/bin/bash
set -e

# build_and_deploy.sh
# Automates dependency installation, compilation of libagar (with macOS patches),
# compilation of OVCC & sub-modules, and packaging into a self-contained app bundle.

echo "========================================="
echo " Starting OVCC Build & Deploy Automation"
echo "========================================="

# 1. System checks
if [[ "$OSTYPE" != "darwin"* ]]; then
  echo "Error: This script is only supported on macOS."
  exit 1
fi

# 2. Check Homebrew
if ! command -v brew &> /dev/null; then
  echo "Error: Homebrew is not installed. Please install Homebrew from https://brew.sh/."
  exit 1
fi

# 3. Install Homebrew packages
echo "--> Checking and installing system dependencies (sdl2, dylibbundler, pkg-config)..."
install_brew_dep() {
  if brew list "$1" &> /dev/null; then
    echo "--> $1 is already installed."
  else
    echo "--> Installing $1..."
    brew install "$1" || echo "--> Warning: Failed to install $1, proceeding anyway..."
  fi
}

install_brew_dep sdl2
install_brew_dep dylibbundler
install_brew_dep pkg-config

# 4. Clone and patch libagar
echo "--> Cloning libagar from GitHub..."
WORKSPACE_DIR="$(pwd)"
TEMP_DIR="${WORKSPACE_DIR}/libagar-src"

rm -rf "${TEMP_DIR}"
git clone https://github.com/JulNadeauCA/libagar.git "${TEMP_DIR}"
cd "${TEMP_DIR}"

echo "--> Checking out libagar version 1.7.1 (commit 11d8355d00a4f8c4cb05bec6496efd55fb121696)..."
git checkout 11d8355d00a4f8c4cb05bec6496efd55fb121696

echo "--> Applying macOS patches to libagar..."
git apply "${WORKSPACE_DIR}/Patches/AGAR/0001-Fix-compile-on-latest-MacOS.patch"
git apply "${WORKSPACE_DIR}/Patches/AGAR/0002-Hack-to-prevent-crashing-on-latest-MacOS.patch"
git apply "${WORKSPACE_DIR}/Patches/AGAR/0003-Add-script-fix-dylibs.sh-for-MacOS.patch"

# Fix undeclared AG_MouseButtonUpdate in drv_cocoa.m
python3 -c "
with open('gui/drv_cocoa.m', 'r') as f:
    content = f.read()
content = content.replace('AG_MouseButtonUpdate(drv->mouse, AG_BUTTON_PRESSED, btn);', 'drv->mouse->btnState |= AG_MOUSE_BUTTON(btn);')
content = content.replace('AG_MouseButtonUpdate(drv->mouse, AG_BUTTON_RELEASED, btn);', 'drv->mouse->btnState &= ~AG_MOUSE_BUTTON(btn);')
with open('gui/drv_cocoa.m', 'w') as f:
    f.write(content)
"

# Fix fix-dylibs.sh to not fail if some dynamic libraries are missing
python3 -c "
with open('fix-dylibs.sh', 'r') as f:
    content = f.read()
wrapper = '''run_install_name_tool() {
  local file=\"\${@: -1}\"
  if [ -f \"\$file\" ]; then
    sudo install_name_tool \"\$@\"
  else
    echo \"--> Skipping install_name_tool for non-existent file: \$file\"
  fi
}
'''
content = content.replace('#! /bin/bash', '#! /bin/bash\\n\\n' + wrapper)
content = content.replace('sudo install_name_tool', 'run_install_name_tool')
with open('fix-dylibs.sh', 'w') as f:
    f.write(content)
"

chmod +x fix-dylibs.sh

echo "--> Configuring libagar..."
BREW_PREFIX=$(brew --prefix 2>/dev/null || echo "/usr/local")
env CFLAGS="-I${BREW_PREFIX}/include" ./configure --with-sdl2 --without-sdl

echo "--> Compiling libagar..."
make depend all

echo "--> Installing libagar (requires administrator/sudo password)..."
sudo make install

echo "--> Fixing libagar dynamic library paths (requires administrator/sudo password)..."
sudo ./fix-dylibs.sh

# Clean up libagar source
cd "${WORKSPACE_DIR}"
rm -rf "${TEMP_DIR}"
echo "--> Checked out and successfully installed patched libagar."

# 5. Compile OVCC & modules
echo "--> Cleaning previous OVCC builds..."
make clean

echo "--> Compiling OVCC and modules..."
make

# 6. Deploy / Package into ovcc.app
echo "--> Packaging application and dependencies into ovcc.app..."
mkdir -p ovcc.app/Contents/libs
mkdir -p ovcc.app/Contents/modules
make install

echo "========================================="
echo " OVCC build and packaging completed successfully!"
echo " The self-contained bundle is located at:"
echo "   ${WORKSPACE_DIR}/ovcc.app"
echo ""
echo " Note: To run the emulator, you will need to place"
echo " the required ROMs (e.g. coco3.rom, disk11.rom) in"
echo " the same directory or within the application contents."
echo "========================================="
