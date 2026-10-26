# cmake is not installed system-wide on the development box, so the build
# toolchain is pinned here instead:  nix-shell --run 'cmake -S . -B build'
{ pkgs ? import <nixpkgs> { } }:

pkgs.mkShell {
  packages = [ pkgs.cmake pkgs.gcc ];
}
