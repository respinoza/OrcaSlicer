#!/bin/bash

set -e
set -o pipefail
SECONDS=0

while getopts ":dpa:snt:xbc:i:1Tuh" opt; do
  case "${opt}" in
    d )
        export BUILD_TARGET="deps"
        ;;
    p )
        export PACK_DEPS="1"
        ;;
    a )
        export ARCH="$OPTARG"
        ;;
    s )
        export BUILD_TARGET="slicer"
        ;;
    n )
        export NIGHTLY_BUILD="1"
        ;;
    t )
        export OSX_DEPLOYMENT_TARGET="$OPTARG"
        ;;
    x )
        export SLICER_CMAKE_GENERATOR="Ninja Multi-Config"
        export SLICER_BUILD_TARGET="all"
        export DEPS_CMAKE_GENERATOR="Ninja"
        ;;
    b )
        export BUILD_ONLY="1"
        ;;
    c )
        export BUILD_CONFIG="$OPTARG"
        ;;
    i )
        export CMAKE_IGNORE_PREFIX_PATH="${CMAKE_IGNORE_PREFIX_PATH:+$CMAKE_IGNORE_PREFIX_PATH;}$OPTARG"
        ;;
    1 )
        export CMAKE_BUILD_PARALLEL_LEVEL=1
        ;;
    T )
        export BUILD_TESTS="1"
        ;;
    u )
        export BUILD_TARGET="universal"
        ;;
    h ) echo "Usage: ./build_release_macos.sh [-d]"
        echo "   -d: Build deps only"
        echo "   -a: Set ARCHITECTURE (arm64 or x86_64 or universal)"
        echo "   -s: Build slicer only"
        echo "   -u: Build universal app only (requires existing arm64 and x86_64 app bundles)"
        echo "   -n: Nightly build"
        echo "   -t: Specify minimum version of the target platform, default is 11.3"
        echo "   -x: Use Ninja Multi-Config CMake generator, default is Xcode"
        echo "   -b: Build without reconfiguring CMake"
        echo "   -c: Set CMake build configuration, default is Release"
        echo "   -i: Add a prefix to ignore during CMake dependency discovery (repeatable), defaults to /opt/local:/usr/local:/opt/homebrew"
        echo "   -1: Use single job for building"
        echo "   -T: Build and run tests"
        exit 0
        ;;
    * )
        ;;
  esac
done

# Set defaults

if [ -z "$ARCH" ]; then
    ARCH="$(uname -m)"
    export ARCH
fi

if [ -z "$BUILD_CONFIG" ]; then
  export BUILD_CONFIG="Release"
fi

if [ -z "$BUILD_TARGET" ]; then
  export BUILD_TARGET="all"
fi

if [ -z "$SLICER_CMAKE_GENERATOR" ]; then
  export SLICER_CMAKE_GENERATOR="Xcode"
fi

if [ -z "$SLICER_BUILD_TARGET" ]; then
  export SLICER_BUILD_TARGET="ALL_BUILD"
fi

if [ -z "$DEPS_CMAKE_GENERATOR" ]; then
  export DEPS_CMAKE_GENERATOR="Unix Makefiles"
fi

if [ -z "$OSX_DEPLOYMENT_TARGET" ]; then
  export OSX_DEPLOYMENT_TARGET="12.0"
fi

if [ -z "$CMAKE_IGNORE_PREFIX_PATH" ]; then
  export CMAKE_IGNORE_PREFIX_PATH="/opt/local:/usr/local:/opt/homebrew"
fi

CMAKE_VERSION=$(cmake --version | head -1 | sed 's/[^0-9]*\([0-9]*\).*/\1/')
if [ "$CMAKE_VERSION" -ge 4 ] 2>/dev/null; then
  export CMAKE_POLICY_VERSION_MINIMUM=3.5
  export CMAKE_POLICY_COMPAT="-DCMAKE_POLICY_VERSION_MINIMUM=3.5"
  echo "Detected CMake 4.x, adding compatibility flag (env + cmake arg)"
else
  export CMAKE_POLICY_COMPAT=""
fi

echo "Build params:"
echo " - ARCH: $ARCH"
echo " - BUILD_CONFIG: $BUILD_CONFIG"
echo " - BUILD_TARGET: $BUILD_TARGET"
echo " - CMAKE_GENERATOR: $SLICER_CMAKE_GENERATOR for Slicer, $DEPS_CMAKE_GENERATOR for deps"
echo " - OSX_DEPLOYMENT_TARGET: $OSX_DEPLOYMENT_TARGET"
echo " - CMAKE_IGNORE_PREFIX_PATH: $CMAKE_IGNORE_PREFIX_PATH"
echo

# if which -s brew; then
# 	brew --prefix libiconv
# 	brew --prefix zstd
# 	export LIBRARY_PATH=$LIBRARY_PATH:$(brew --prefix zstd)/lib/
# elif which -s port; then
# 	port install libiconv
# 	port install zstd
# 	export LIBRARY_PATH=$LIBRARY_PATH:/opt/local/lib
# else
# 	echo "Need either brew or macports to successfully build deps"
# 	exit 1
# fi

PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_BUILD_DIR="$PROJECT_DIR/build/$ARCH"
DEPS_DIR="$PROJECT_DIR/deps"

# For Multi-config generators like Ninja and Xcode
export BUILD_DIR_CONFIG_SUBDIR="/$BUILD_CONFIG"

function build_deps() {
    # iterate over two architectures: x86_64 and arm64
    for _ARCH in x86_64 arm64; do
        # if ARCH is universal or equal to _ARCH
        if [ "$ARCH" == "universal" ] || [ "$ARCH" == "$_ARCH" ]; then

            PROJECT_BUILD_DIR="$PROJECT_DIR/build/$_ARCH"
            DEPS_BUILD_DIR="$DEPS_DIR/build/$_ARCH"
            DEPS="$DEPS_BUILD_DIR/OrcaSlicer_dep"

            echo "Building deps..."
            (
                set -x
                mkdir -p "$DEPS"
                cd "$DEPS_BUILD_DIR"
                if [ "1." != "$BUILD_ONLY". ]; then
                    cmake "${DEPS_DIR}" \
                        -G "${DEPS_CMAKE_GENERATOR}" \
                        -DCMAKE_BUILD_TYPE="$BUILD_CONFIG" \
                        -DCMAKE_OSX_ARCHITECTURES:STRING="${_ARCH}" \
                        -DCMAKE_OSX_DEPLOYMENT_TARGET="${OSX_DEPLOYMENT_TARGET}" \
                        -DCMAKE_IGNORE_PREFIX_PATH="${CMAKE_IGNORE_PREFIX_PATH}" \
                        ${CMAKE_POLICY_COMPAT}
                fi
                cmake --build . --config "$BUILD_CONFIG" --target deps
            )
        fi
    done
}

function pack_deps() {
    echo "Packing deps..."
    (
        set -x
        cd "$DEPS_DIR"
        tar -zcvf "OrcaSlicer_dep_mac_${ARCH}_$(date +"%Y%m%d").tar.gz" "build"
    )
}

function build_slicer() {
    # iterate over two architectures: x86_64 and arm64
    for _ARCH in x86_64 arm64; do
        # if ARCH is universal or equal to _ARCH
        if [ "$ARCH" == "universal" ] || [ "$ARCH" == "$_ARCH" ]; then

            PROJECT_BUILD_DIR="$PROJECT_DIR/build/$_ARCH"
            DEPS_BUILD_DIR="$DEPS_DIR/build/$_ARCH"
            DEPS="$DEPS_BUILD_DIR/OrcaSlicer_dep"

            echo "Building slicer for $_ARCH..."
            (
                set -x
            mkdir -p "$PROJECT_BUILD_DIR"
            cd "$PROJECT_BUILD_DIR"
            if [ "1." != "$BUILD_ONLY". ]; then
                cmake "${PROJECT_DIR}" \
                    -G "${SLICER_CMAKE_GENERATOR}" \
                    -DORCA_TOOLS=ON \
                    ${ORCA_UPDATER_SIG_KEY:+-DORCA_UPDATER_SIG_KEY="$ORCA_UPDATER_SIG_KEY"} \
                    ${BUILD_TESTS:+-DBUILD_TESTS=ON} \
                    -DCMAKE_INSTALL_PREFIX="$PWD/Snapmaker_Orca" \
                    -DCMAKE_BUILD_TYPE="$BUILD_CONFIG" \
                    -DCMAKE_OSX_ARCHITECTURES="${_ARCH}" \
                    -DCMAKE_OSX_DEPLOYMENT_TARGET="${OSX_DEPLOYMENT_TARGET}" \
                    -DCMAKE_IGNORE_PREFIX_PATH="${CMAKE_IGNORE_PREFIX_PATH}" \
                    ${CMAKE_POLICY_COMPAT}
            fi
            cmake --build . --config "$BUILD_CONFIG" --target "$SLICER_BUILD_TARGET"
            # Explicitly build profile_validator if ORCA_TOOLS is enabled
            if [ "$SLICER_BUILD_TARGET" = "all" ] || [ "$SLICER_BUILD_TARGET" = "ALL_BUILD" ]; then
                cmake --build . --config "$BUILD_CONFIG" --target Snapmaker_Orca_profile_validator || echo "Warning: Snapmaker_Orca_profile_validator build failed or not available"
            fi
        )

        if [ "1." == "$BUILD_TESTS". ]; then
            echo "Running tests for $_ARCH..."
            (
                set -x
                cd "$PROJECT_BUILD_DIR"
                ctest --build-config "$BUILD_CONFIG" --output-on-failure
            )
        fi

        echo "Verify localization with gettext..."
        (
            cd "$PROJECT_DIR"
            ./scripts/run_gettext.sh
        )

    echo "Fix macOS app package..."
    (
        cd "$PROJECT_BUILD_DIR"
        mkdir -p Snapmaker_Orca
        cd Snapmaker_Orca
        # remove previously built app
        rm -rf "./Snapmaker Orca.app"
        # determine source app path (handle both space and underscore names)
        APP_SOURCE_PATH="../src$BUILD_DIR_CONFIG_SUBDIR/Snapmaker Orca.app"
        if [ ! -d "$APP_SOURCE_PATH" ]; then
            APP_SOURCE_PATH="../src$BUILD_DIR_CONFIG_SUBDIR/Snapmaker_Orca.app"
        fi
        if [ ! -d "$APP_SOURCE_PATH" ]; then
            echo "Error: cannot find built app bundle at $APP_SOURCE_PATH"
            exit 1
        fi
        # fully copy newly built app (rename to canonical name with space)
        cp -pR "$APP_SOURCE_PATH" "./Snapmaker Orca.app"
        # fix resources
        resources_path=$(readlink "./Snapmaker Orca.app/Contents/Resources")
        rm "./Snapmaker Orca.app/Contents/Resources"
        cp -R "$resources_path" "./Snapmaker Orca.app/Contents/Resources"
        # delete .DS_Store file
        find "./Snapmaker Orca.app/" -name '.DS_Store' -delete

        # Copy Sentry crashpad_handler and libsentry.dylib for crash reporting
        CRASHPAD_HANDLER="${DEPS}/usr/local/bin/crashpad_handler"
        LIBSENTRY="${DEPS}/usr/local/lib/libsentry.dylib"
        APP_MACOS_DIR='./Snapmaker Orca.app/Contents/MacOS'
        APP_FRAMEWORKS_DIR='./Snapmaker Orca.app/Contents/Frameworks'
        EXECUTABLE="${APP_MACOS_DIR}/Snapmaker_Orca"
        
        if [ -f "${CRASHPAD_HANDLER}" ]; then
            echo "Copying crashpad_handler to app bundle..."
            cp -f "${CRASHPAD_HANDLER}" "${APP_MACOS_DIR}/crashpad_handler"
            # Sign crashpad_handler
            codesign --force --sign - "${APP_MACOS_DIR}/crashpad_handler" 2>/dev/null || true
        else
            echo "Warning: crashpad_handler not found at ${CRASHPAD_HANDLER}"
        fi
        
        if [ -f "${LIBSENTRY}" ]; then
            echo "Copying libsentry.dylib to Frameworks..."
            mkdir -p "${APP_FRAMEWORKS_DIR}"
            cp -f "${LIBSENTRY}" "${APP_FRAMEWORKS_DIR}/libsentry.dylib"
            # Sign libsentry.dylib
            codesign --force --sign - "${APP_FRAMEWORKS_DIR}/libsentry.dylib" 2>/dev/null || true
            
            # Update rpath in Snapmaker_Orca to use @executable_path relative path
            if [ -f "${EXECUTABLE}" ]; then
                echo "Updating libsentry.dylib rpath in Snapmaker_Orca..."
                install_name_tool -change "@rpath/libsentry.dylib" "@executable_path/../Frameworks/libsentry.dylib" "${EXECUTABLE}" 2>/dev/null || true
                # Re-sign the executable after modification
                codesign --force --sign - "${EXECUTABLE}" 2>/dev/null || true
            fi
        else
            echo "Warning: libsentry.dylib not found at ${LIBSENTRY}"
        fi

        # Copy Snapmaker_Orca_profile_validator.app if it exists
        if [ -f "../src$BUILD_DIR_CONFIG_SUBDIR/Snapmaker_Orca_profile_validator.app/Contents/MacOS/Snapmaker_Orca_profile_validator" ]; then
            echo "Copying Snapmaker_Orca_profile_validator.app..."
            rm -rf ./Snapmaker_Orca_profile_validator.app
            cp -pR "../src$BUILD_DIR_CONFIG_SUBDIR/Snapmaker_Orca_profile_validator.app" ./Snapmaker_Orca_profile_validator.app
            # delete .DS_Store file
            find ./Snapmaker_Orca_profile_validator.app/ -name '.DS_Store' -delete
        fi

        # Generate dSYM debug symbols for debugging and Sentry crash reporting
        # Always generate dSYM files - they are useful for crash analysis even without Sentry
        echo "Generating dSYM debug symbols..."
        DSYM_DIR="./dSYM"
        mkdir -p "${DSYM_DIR}"
        
        # Generate dSYM for main app
        if [ -f "${APP_MACOS_DIR}/Snapmaker_Orca" ]; then
            echo "Generating dSYM for Snapmaker_Orca..."
            dsymutil "${APP_MACOS_DIR}/Snapmaker_Orca" -o "${DSYM_DIR}/Snapmaker_Orca.dSYM" 2>/dev/null || echo "Warning: Failed to generate dSYM for Snapmaker_Orca (no debug symbols?)"
        fi
        
        # Generate dSYM for crashpad_handler if it exists
        if [ -f "${APP_MACOS_DIR}/crashpad_handler" ]; then
            echo "Generating dSYM for crashpad_handler..."
            dsymutil "${APP_MACOS_DIR}/crashpad_handler" -o "${DSYM_DIR}/crashpad_handler.dSYM" 2>/dev/null || true
        fi
        
        # Generate dSYM for libsentry.dylib if it exists
        if [ -f "${APP_FRAMEWORKS_DIR}/libsentry.dylib" ]; then
            echo "Generating dSYM for libsentry.dylib..."
            dsymutil "${APP_FRAMEWORKS_DIR}/libsentry.dylib" -o "${DSYM_DIR}/libsentry.dSYM" 2>/dev/null || true
        fi
        
        # Generate dSYM for profile_validator if it exists
        if [ -f "./Snapmaker_Orca_profile_validator.app/Contents/MacOS/Snapmaker_Orca_profile_validator" ]; then
            echo "Generating dSYM for Snapmaker_Orca_profile_validator..."
            dsymutil "./Snapmaker_Orca_profile_validator.app/Contents/MacOS/Snapmaker_Orca_profile_validator" -o "${DSYM_DIR}/Snapmaker_Orca_profile_validator.dSYM" 2>/dev/null || true
        fi
        
        echo "dSYM files generated in ${DSYM_DIR}"
        ls -la "${DSYM_DIR}" 2>/dev/null || echo "No dSYM files generated"
    )

    # extract version
    # export ver=$(grep '^#define Snapmaker_VERSION' ../src/libslic3r/libslic3r_version.h | cut -d ' ' -f3)
    # ver="_V${ver//\"}"
    # echo $PWD
    # if [ "1." != "$NIGHTLY_BUILD". ];
    # then
    #     ver=${ver}_dev
    # fi

        # zip -FSr Snapmaker_Orca${ver}_Mac_${_ARCH}.zip OrcaSlicer.app

    fi
    done
}

function lipo_dir() {
    local universal_dir="$1"
    local x86_64_dir="$2"

    # Find all Mach-O files in the universal (arm64-based) copy and lipo them
    while IFS= read -r -d '' f; do
        local rel="${f#"$universal_dir"/}"
        local x86="$x86_64_dir/$rel"
        if [ -f "$x86" ]; then
            echo "  lipo: $rel"
            lipo -create "$f" "$x86" -output "$f.tmp"
            mv "$f.tmp" "$f"
        else
            echo "  warning: no x86_64 counterpart for $rel, keeping arm64 only"
        fi
    done < <(find "$universal_dir" -type f -print0 | while IFS= read -r -d '' candidate; do
        if file "$candidate" | grep -q "Mach-O"; then
            printf '%s\0' "$candidate"
        fi
    done)
}

function build_universal() {
    echo "Building universal binary..."

    PROJECT_BUILD_DIR="$PROJECT_DIR/build/$ARCH"
    ARM64_APP="$PROJECT_DIR/build/arm64/Snapmaker_Orca/Snapmaker Orca.app"
    X86_64_APP="$PROJECT_DIR/build/x86_64/Snapmaker_Orca/Snapmaker Orca.app"

    mkdir -p "$PROJECT_BUILD_DIR/Snapmaker_Orca"
    UNIVERSAL_APP="$PROJECT_BUILD_DIR/Snapmaker_Orca/Snapmaker Orca.app"
    rm -rf "$UNIVERSAL_APP"
    cp -R "$ARM64_APP" "$UNIVERSAL_APP"

    # Get the binary path inside the .app bundle
    BINARY_PATH="Contents/MacOS/Snapmaker_Orca"

    # lipo_dir merges every Mach-O file of the bundle, including Sentry's
    # crashpad_handler and libsentry.dylib (kept arm64-only if x86_64 lacks them)
    echo "Creating universal binaries for Snapmaker Orca.app..."
    lipo_dir "$UNIVERSAL_APP" "$X86_64_APP"
    echo "Universal Snapmaker Orca.app created at $UNIVERSAL_APP"

    # Re-sign the merged Sentry artifacts and make the universal executable load
    # libsentry.dylib from the bundle's Frameworks directory
    CRASHPAD_UNIVERSAL="${UNIVERSAL_APP}/Contents/MacOS/crashpad_handler"
    if [ -f "${CRASHPAD_UNIVERSAL}" ]; then
        codesign --force --sign - "${CRASHPAD_UNIVERSAL}" 2>/dev/null || true
    fi
    LIBSENTRY_UNIVERSAL="${UNIVERSAL_APP}/Contents/Frameworks/libsentry.dylib"
    if [ -f "${LIBSENTRY_UNIVERSAL}" ]; then
        codesign --force --sign - "${LIBSENTRY_UNIVERSAL}" 2>/dev/null || true
        echo "Updating libsentry.dylib rpath in universal Snapmaker_Orca..."
        install_name_tool -change "@rpath/libsentry.dylib" "@executable_path/../Frameworks/libsentry.dylib" "${UNIVERSAL_APP}/${BINARY_PATH}" 2>/dev/null || true
        codesign --force --sign - "${UNIVERSAL_APP}/${BINARY_PATH}" 2>/dev/null || true
    fi

    # Create universal binary for profile validator if it exists
    ARM64_VALIDATOR="$PROJECT_DIR/build/arm64/Snapmaker_Orca/Snapmaker_Orca_profile_validator.app"
    X86_64_VALIDATOR="$PROJECT_DIR/build/x86_64/Snapmaker_Orca/Snapmaker_Orca_profile_validator.app"
    if [ -d "$ARM64_VALIDATOR" ] && [ -d "$X86_64_VALIDATOR" ]; then
        echo "Creating universal binaries for Snapmaker_Orca_profile_validator.app..."
        UNIVERSAL_VALIDATOR_APP="$PROJECT_BUILD_DIR/Snapmaker_Orca/Snapmaker_Orca_profile_validator.app"
        rm -rf "$UNIVERSAL_VALIDATOR_APP"
        cp -R "$ARM64_VALIDATOR" "$UNIVERSAL_VALIDATOR_APP"
        lipo_dir "$UNIVERSAL_VALIDATOR_APP" "$X86_64_VALIDATOR"
        # Binary path inside the profile validator .app bundle (used for its dSYM below)
        VALIDATOR_BINARY_PATH="Contents/MacOS/Snapmaker_Orca_profile_validator"
        echo "Universal Snapmaker_Orca_profile_validator.app created at $UNIVERSAL_VALIDATOR_APP"
    fi
    
    # Generate dSYM for universal binary - always generate for debugging and Sentry crash reporting
    echo "Generating dSYM for universal binary..."
    DSYM_DIR="$PROJECT_BUILD_DIR/Snapmaker_Orca/dSYM"
    mkdir -p "${DSYM_DIR}"
    
    # Generate dSYM for universal main app
    if [ -f "$UNIVERSAL_APP/$BINARY_PATH" ]; then
        echo "Generating dSYM for universal Snapmaker_Orca..."
        dsymutil "$UNIVERSAL_APP/$BINARY_PATH" -o "${DSYM_DIR}/Snapmaker_Orca.dSYM" 2>/dev/null || echo "Warning: Failed to generate dSYM for universal Snapmaker_Orca"
    fi
    
    # Generate dSYM for universal crashpad_handler if it exists
    if [ -f "$UNIVERSAL_APP/Contents/MacOS/crashpad_handler" ]; then
        echo "Generating dSYM for universal crashpad_handler..."
        dsymutil "$UNIVERSAL_APP/Contents/MacOS/crashpad_handler" -o "${DSYM_DIR}/crashpad_handler.dSYM" 2>/dev/null || true
    fi
    
    # Generate dSYM for universal libsentry.dylib if it exists
    if [ -f "$UNIVERSAL_APP/Contents/Frameworks/libsentry.dylib" ]; then
        echo "Generating dSYM for universal libsentry.dylib..."
        dsymutil "$UNIVERSAL_APP/Contents/Frameworks/libsentry.dylib" -o "${DSYM_DIR}/libsentry.dSYM" 2>/dev/null || true
    fi
    
    # Generate dSYM for universal profile_validator if it exists
    if [ -f "$UNIVERSAL_VALIDATOR_APP/$VALIDATOR_BINARY_PATH" ]; then
        echo "Generating dSYM for universal Snapmaker_Orca_profile_validator..."
        dsymutil "$UNIVERSAL_VALIDATOR_APP/$VALIDATOR_BINARY_PATH" -o "${DSYM_DIR}/Snapmaker_Orca_profile_validator.dSYM" 2>/dev/null || true
    fi
    
    echo "Universal dSYM files generated in ${DSYM_DIR}"
    ls -la "${DSYM_DIR}" 2>/dev/null || echo "No dSYM files generated"
}

case "${BUILD_TARGET}" in
    all)
        build_deps
        build_slicer
        ;;
    deps)
        build_deps
        ;;
    slicer)
        build_slicer
        ;;
    universal)
        build_universal
        ;;
    *)
        echo "Unknown target: $BUILD_TARGET. Available targets: deps, slicer, universal, all."
        exit 1
        ;;
esac

if [ "$ARCH" = "universal" ] && { [ "$BUILD_TARGET" = "all" ] || [ "$BUILD_TARGET" = "slicer" ]; }; then
    build_universal
fi

if [ "1." == "$PACK_DEPS". ]; then
    pack_deps
fi

elapsed=$SECONDS
printf "\nBuild completed in %dh %dm %ds\n" $((elapsed/3600)) $((elapsed%3600/60)) $((elapsed%60))
