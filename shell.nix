{
  pkgs ? import <nixpkgs> { },
}:
let
  project = import ./. { inherit pkgs; };
in
pkgs.mkShell {

  inputsFrom = [ project ];

  nativeBuildInputs = with pkgs; [
    llvmPackages_19.clang-tools
    doxygen
  ];

  shellHook = ''
    unset QT_PLUGIN_PATH
    unset QT_QPA_PLATFORM_PLUGIN_PATH

    export NIX_BUILD_TOP="$PWD/build"
    export GEN_DATE=$(date -I)

    alias configure="cmake -DCMAKE_EXPORT_COMPILE_COMMANDS=1 -DBUILD_WITH_QT6=1 -DBUILD_TESTS=1 -DCHATTERINO_SPELLCHECK=1 .."
    alias nightly="echo "nightly" > $NIX_BUILD_TOP/bin/modes"

    mkdir -p "$NIX_BUILD_TOP"
    cd $NIX_BUILD_TOP
  '';
}
