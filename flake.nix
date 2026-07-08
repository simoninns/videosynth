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
          ];

          doCheck = true;
          checkPhase = ''
            ctest --output-on-failure
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
            llvmPackages_18.clang-tools
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
