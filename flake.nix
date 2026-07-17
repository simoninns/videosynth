{
  description = "VideoSynth MVP foundation";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-26.05";
    flake-utils.url = "github:numtide/flake-utils";
  };

  outputs = { self, nixpkgs, flake-utils }:
    flake-utils.lib.eachDefaultSystem (system:
      let
        pkgs = import nixpkgs { inherit system; };
        # Git metadata is unavailable inside the Nix build sandbox, so the
        # commit hash is passed in as a version override instead.
        versionString = self.shortRev or self.dirtyShortRev or "0.1.0";
      in {
        packages.default = pkgs.stdenv.mkDerivation {
          pname = "videosynth";
          version = versionString;
          src = self;

          nativeBuildInputs = with pkgs; [
            cmake
            ninja
            pkg-config
            qt6.wrapQtAppsHook
          ];

          buildInputs = with pkgs; [
            yaml-cpp
            spdlog
            gtest
            sqlite
            openexr
            zlib
            ffmpeg
            qt6.qtbase
            qt6.qtsvg
          ];

          cmakeFlags = [
            "-DCMAKE_BUILD_TYPE=Release"
            "-DBUILD_TESTING=ON"
            "-DPROJECT_VERSION_OVERRIDE=${versionString}"
            # The build sandbox only exposes the stdenv's unwrapped clang-tidy,
            # which cannot resolve libc++ headers. clang-tidy is a dev/CI gate.
            "-DVIDEOSYNTH_ENABLE_CLANG_TIDY=OFF"
            # Point the bundled asset root at the installed data directory so the
            # binary resolves "{bundled}" assets after the sandbox is gone. The
            # assets themselves are only present with a submodule-aware build
            # (nix run '.?submodules=1'); VIDEOSYNTH_ASSET_DIR overrides at runtime.
            "-DVIDEOSYNTH_BUNDLED_ASSET_DIR=${placeholder "out"}/share/videosynth/assets"
          ];

          doCheck = true;
          # The hermetic sandbox has no submodule assets, so run only the fast,
          # mocked unit lane here (AGENTS.md §3.1). Functional suites load real
          # media from the videosynth-assets submodule and run in the dev shell
          # or a dedicated CI job where that submodule is checked out.
          checkPhase = ''
            ctest -L unit --output-on-failure
          '';
        };

        checks.default = self.packages.${system}.default;

        devShells.default = pkgs.mkShell {
          packages = with pkgs; [
            cmake
            ninja
            pkg-config
            python3
            python3Packages.numpy
            gcc
            yaml-cpp
            spdlog
            gtest
            sqlite
            openexr
            zlib
            ffmpeg
            qt6.qtbase
            qt6.qtsvg
            clang-tools
            ccache
          ];

          # Qt needs its platform plugins on the path when the GUI is run
          # directly from the dev shell (outside wrapQtAppsHook).
          shellHook = ''
            export QT_PLUGIN_PATH="${pkgs.qt6.qtbase}/lib/qt-6/plugins''${QT_PLUGIN_PATH:+:$QT_PLUGIN_PATH}"
          '';
        };
      });
}
