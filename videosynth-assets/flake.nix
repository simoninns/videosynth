{
  description = "Development shell for verifying videosynth BT.601 assets";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
  };

  outputs = { self, nixpkgs }:
    let
      systems = [
        "x86_64-linux"
        "aarch64-linux"
      ];

      forAllSystems = f:
        builtins.listToAttrs (map (system: {
          name = system;
          value = f system;
        }) systems);
    in
    {
      devShells = forAllSystems (system:
        let
          pkgs = import nixpkgs { inherit system; };
        in
        {
          default = pkgs.mkShell {
            packages = [
              pkgs.ffmpeg
              pkgs.openexr
              pkgs.python3
            ];

            shellHook = ''
              echo "videosynth-assets dev shell ready"
              echo "Available tools: $(command -v ffprobe) $(command -v ffmpeg) $(command -v exrheader)"
            '';
          };
        });
    };
}