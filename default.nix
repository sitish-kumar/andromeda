let
  inherit (builtins) fromJSON readFile;

  lock = fromJSON (readFile ./flake.lock);
  namedNode = lock.nodes.${lock.nodes.root.inputs.nixpkgs}.locked;
  nixpkgs = fetchTarball {
    inherit (namedNode) url;
    sha256 = namedNode.narHash;
  };
in
{
  pkgs ? import nixpkgs { },
}:
let
  inherit (builtins) substring;
  inherit (pkgs.lib) cleanSource mkDefault concatStringsSep;

  src = fetchGit ./.;

  mkDate =
    longDate:
    concatStringsSep "-" [
      (substring 0 4 longDate)
      (substring 4 2 longDate)
      (substring 6 2 longDate)
    ];

  shortRev = src.shortRev or "dirty";

  package = (pkgs.callPackage ./nix/package.nix { }) {
    inherit shortRev;
    version = mkDate (src.lastModifiedDate or "19700101") + "_" + shortRev;
    src = cleanSource src;
  };
in
{
  hjemModule = {
    imports = [ ./nix/hjem-module.nix ];
    programs.noctalia.package = mkDefault package;
  };

  homeModule = {
    imports = [ ./nix/home-module.nix ];
    programs.noctalia.package = mkDefault package;
  };

  inherit package;
}
