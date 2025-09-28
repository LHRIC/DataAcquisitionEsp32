{
  description = "Simple ESP-IDF dev shell (with esp-clang)";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
    esp-dev = {
      url = "github:mirrexagon/nixpkgs-esp-dev";
      inputs.nixpkgs.follows = "nixpkgs";
    };
  };

  outputs =
    { nixpkgs, esp-dev, ... }:
    let
      system = "x86_64-linux";
      pkgs = import nixpkgs {
        inherit system;
        overlays = [ esp-dev.overlays.default ];
      };
    in
    {
      devShells.${system}.default = pkgs.mkShell {
        packages = with pkgs; [
          esp-idf-full
          cmake
          ninja
          python3
          gnumake
          git
        ];

        shellHook = ''
          # Source ESP-IDF environment (sets IDF_PATH, IDF_TOOLS_PATH, PATH, etc.)
          . "$IDF_PATH/export.sh"
          echo "ESP-IDF ready. Run: idf.py build / idf.py flash"
        '';
      };
    };
}
