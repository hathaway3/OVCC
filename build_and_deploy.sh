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
install_brew_dep freetype

# 4. Clone and patch libagar if not already installed
WORKSPACE_DIR="$(pwd)"
AGAR_PREFIX="${WORKSPACE_DIR}/libagar-install"
export PATH="${AGAR_PREFIX}/bin:${PATH}"

build_libagar=true
# NB: a valid install must include the SHARED libraries (libag_*.dylib). A
# static-only install (missing dylibs) must be rebuilt, or AGAR gets linked
# statically into ovcc and every plugin, breaking plugin menu integration.
if [ -f "${AGAR_PREFIX}/bin/agar-config" ] && [ "$("${AGAR_PREFIX}/bin/agar-config" --version)" = "1.7.1" ] && [ -f "${AGAR_PREFIX}/lib/libag_gui.8.dylib" ]; then
  echo "--> Patched libagar 1.7.1 (shared) is already installed in project-local prefix."
  build_libagar=false
elif command -v agar-config &> /dev/null && [ "$(agar-config --version)" = "1.7.1" ] && [ -f "$(agar-config --prefix)/lib/libag_gui.8.dylib" ]; then
  echo "--> libagar 1.7.1 (shared) is already installed in the system."
  build_libagar=false
fi

if [ "$build_libagar" = true ]; then
  echo "--> Cloning libagar from GitHub..."
  TEMP_DIR="${WORKSPACE_DIR}/libagar-src"
  rm -rf "${TEMP_DIR}"
  git clone https://github.com/JulNadeauCA/libagar.git "${TEMP_DIR}"
  cd "${TEMP_DIR}"

  echo "--> Checking out libagar version 1.7.1 (commit 11d8355d00a4f8c4cb05bec6496efd55fb121696)..."
  git checkout 11d8355d00a4f8c4cb05bec6496efd55fb121696

  echo "--> Applying macOS patches to libagar..."
  git apply "${WORKSPACE_DIR}/Patches/AGAR/0001-Fix-compile-on-latest-MacOS.patch"
  git apply "${WORKSPACE_DIR}/Patches/AGAR/0002-Hack-to-prevent-crashing-on-latest-MacOS.patch"
  # Skip 0003 patch as we generate fix-dylibs.sh dynamically below
  git apply "${WORKSPACE_DIR}/Patches/AGAR/0004-Fix-undeclared-AG_MouseButtonUpdate-in-drv_cocoa.patch"
  # 0005: fix the macOS shared-dylib link (bad -rpath spacing) and give the
  # dylibs absolute install names + header padding, so they actually build as
  # shared libraries and dylibbundler can relocate them into the .app.
  git apply "${WORKSPACE_DIR}/Patches/AGAR/0005-Fix-macOS-shared-dylib-rpath-and-install-name.patch"

  # Generate fix-dylibs.sh locally with wrapper and prefix support
  cat << 'EOF' > fix-dylibs.sh
#!/bin/bash
PREFIX="${1:-/usr/local}"
echo "--> Fixing libagar dylibs under prefix: ${PREFIX}"

run_install_name_tool() {
  local file="${@: -1}"
  if [ -f "$file" ]; then
    install_name_tool "$@"
  else
    echo "--> Skipping install_name_tool for non-existent file: $file"
  fi
}

run_install_name_tool -id "${PREFIX}/lib/libag_core.dylib" "${PREFIX}/lib/libag_core.8.dylib"
run_install_name_tool -id "${PREFIX}/lib/libag_gui.dylib" "${PREFIX}/lib/libag_gui.8.dylib"
run_install_name_tool -id "${PREFIX}/lib/libag_math.dylib" "${PREFIX}/lib/libag_math.8.dylib"
run_install_name_tool -id "${PREFIX}/lib/libag_net.dylib" "${PREFIX}/lib/libag_net.8.dylib"
run_install_name_tool -id "${PREFIX}/lib/libag_sg.dylib" "${PREFIX}/lib/libag_sg.8.dylib"
run_install_name_tool -id "${PREFIX}/lib/libag_sk.dylib" "${PREFIX}/lib/libag_sk.8.dylib"
run_install_name_tool -id "${PREFIX}/lib/libag_vg.dylib" "${PREFIX}/lib/libag_vg.8.dylib"
run_install_name_tool -change 'libag_core.dylib' "${PREFIX}/lib/libag_core.dylib" "${PREFIX}/lib/libag_gui.8.dylib"
run_install_name_tool -change 'libag_core.dylib' "${PREFIX}/lib/libag_core.dylib" "${PREFIX}/lib/libag_math.8.dylib"
run_install_name_tool -change 'libag_gui.dylib' "${PREFIX}/lib/libag_gui.dylib" "${PREFIX}/lib/libag_math.8.dylib"
run_install_name_tool -change 'libag_core.dylib' "${PREFIX}/lib/libag_core.dylib" "${PREFIX}/lib/libag_net.8.dylib"
run_install_name_tool -change 'libag_core.dylib' "${PREFIX}/lib/libag_core.dylib" "${PREFIX}/lib/libag_sg.8.dylib"
run_install_name_tool -change 'libag_gui.dylib' "${PREFIX}/lib/libag_gui.dylib" "${PREFIX}/lib/libag_sg.8.dylib"
run_install_name_tool -change 'libag_math.dylib' "${PREFIX}/lib/libag_math.dylib" "${PREFIX}/lib/libag_sg.8.dylib"
run_install_name_tool -change 'libag_math.dylib' "${PREFIX}/lib/libag_math.dylib" "${PREFIX}/lib/libag_sk.8.dylib"
run_install_name_tool -change 'libag_gui.dylib' "${PREFIX}/lib/libag_gui.dylib" "${PREFIX}/lib/libag_sk.8.dylib"
run_install_name_tool -change 'libag_core.dylib' "${PREFIX}/lib/libag_core.dylib" "${PREFIX}/lib/libag_sk.8.dylib"
run_install_name_tool -change 'libag_core.dylib' "${PREFIX}/lib/libag_core.dylib" "${PREFIX}/lib/libag_vg.8.dylib"
run_install_name_tool -change 'libag_gui.dylib' "${PREFIX}/lib/libag_gui.dylib" "${PREFIX}/lib/libag_vg.8.dylib"
EOF

  chmod +x fix-dylibs.sh

  echo "--> Configuring libagar..."
  BREW_PREFIX=$(brew --prefix 2>/dev/null || echo "/usr/local")
  # --enable-shared is required: its default ([check]) can silently resolve to
  # static-only, which then gets linked into ovcc AND every plugin as a private
  # copy of AGAR's global state, breaking plugin<->host menu integration.
  env CFLAGS="-I${BREW_PREFIX}/include" ./configure --prefix="${AGAR_PREFIX}" --with-sdl2 --without-sdl --enable-shared

  echo "--> Compiling libagar..."
  make depend all

  echo "--> Installing libagar to local prefix..."
  make install

  echo "--> Fixing libagar dynamic library paths..."
  ./fix-dylibs.sh "${AGAR_PREFIX}"

  # Clean up libagar source
  cd "${WORKSPACE_DIR}"
  rm -rf "${TEMP_DIR}"
  echo "--> Checked out and successfully installed patched libagar locally."
else
  echo "--> Skipping libagar build."
fi

# 5. Compile OVCC & modules
echo "--> Cleaning previous OVCC builds..."
make clean

echo "--> Compiling OVCC and modules..."
make

# 6. Deploy / Package into ovcc.app
echo "--> Packaging application and dependencies into ovcc.app..."
mkdir -p ovcc.app/Contents/MacOS
mkdir -p ovcc.app/Contents/Frameworks
mkdir -p ovcc.app/Contents/PlugIns
make install

# Clean up legacy folders if they exist
rm -rf ovcc.app/Contents/libs
rm -rf ovcc.app/Contents/modules

# If libSDL2-2.0.0.dylib was bundled, check if we need to copy libSDL3.dylib (for sdl2-compat)
if [ -f ovcc.app/Contents/Frameworks/libSDL2-2.0.0.dylib ]; then
  BREW_PREFIX=$(brew --prefix 2>/dev/null || echo "/opt/homebrew")
  if [ -f "${BREW_PREFIX}/lib/libSDL3.dylib" ]; then
    echo "--> Copying libSDL3.dylib (sdl2-compat dependency)..."
    cp -L "${BREW_PREFIX}/lib/libSDL3.dylib" ovcc.app/Contents/Frameworks/libSDL3.dylib
    chmod +w ovcc.app/Contents/Frameworks/libSDL3.dylib
    install_name_tool -id "@rpath/libSDL3.dylib" ovcc.app/Contents/Frameworks/libSDL3.dylib
  fi
fi

# Ad-hoc sign the app bundle
if command -v codesign &> /dev/null; then
  echo "--> Ad-hoc signing the app bundle..."
  codesign --force --deep -s - ovcc.app
fi

echo "========================================="
echo " OVCC build and packaging completed successfully!"
echo " The self-contained bundle is located at:"
echo "   ${WORKSPACE_DIR}/ovcc.app"
echo ""
echo " Note: To run the emulator, you will need to place"
echo " the required ROMs (e.g. coco3.rom, disk11.rom) in"
echo " the same directory or within the application contents."
echo "========================================="
