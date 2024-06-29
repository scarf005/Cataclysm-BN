{
  description = "A Nix-flake-based C/C++ development environment";

  inputs.nixpkgs.url = "https://flakehub.com/f/NixOS/nixpkgs/0.1.*.tar.gz";

  outputs = { self, nixpkgs }:
    let
      supportedSystems = [ "x86_64-linux" "aarch64-linux" "x86_64-darwin" "aarch64-darwin" ];
      forEachSupportedSystem = f: nixpkgs.lib.genAttrs supportedSystems (system: f {
        pkgs = import nixpkgs { inherit system; };
      });
    in
    {
      devShells = forEachSupportedSystem ({ pkgs }: {
        default = pkgs.mkShell.override
          {
            # Override stdenv in order to change compiler:
            stdenv = pkgs.clangStdenv;
          }
          {
            packages = with pkgs; [
              astyle
              clang-tools
              cmake
              ninja
              ccache
              SDL2
              SDL2_image
              SDL2_image
              SDL2_ttf
              SDL2_image
              SDL2_mixer
              SDL2_sound
              SDL2_mixer
              gettext
              ccache
              parallel
              libbacktrace
              plocate
              flac
              # cppcheck
              # doxygen
              # gtest
              # lcov
            ]
            ++ (if pkgs.stdenv.isLinux then [ lldb ] else [ ])
            ++ (if system == "x86_64-windows" then [ vcpkg vcpkg-tool] else [ ])
            ++ (if system == "aarch64-darwin" then [ ] else [ gdb ]);
          };
      });
    };
}

