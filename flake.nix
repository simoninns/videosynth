{
  description = "VideoSynth MVP foundation";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-25.11";
    flake-utils.url = "github:numtide/flake-utils";
  };

  outputs = { self, nixpkgs, flake-utils }:
    flake-utils.lib.eachDefaultSystem (system:
      let
        pkgs = import nixpkgs { inherit system; };
      in {
        packages.default = pkgs.stdenv.mkDerivation {
          pname = "videosynth";
          version = "0.1.0";
          src = self;

          nativeBuildInputs = with pkgs; [
            cmake
            ninja
            pkg-config
          ];

          buildInputs = with pkgs; [
            yaml-cpp
            spdlog
            gtest
            sqlite
            libpng
            zlib
          ];

          cmakeFlags = [
            "-DCMAKE_BUILD_TYPE=Release"
            "-DBUILD_TESTING=ON"
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
            libpng
            zlib
          ];
        };
      });
}
