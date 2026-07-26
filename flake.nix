{
  description = "Configuration utility for DMR radios (Baofeng DM-32UV, TYT, Anytone, Radioddity, etc.)";

  inputs.nixpkgs.url = "github:NixOS/nixpkgs/release-24.11";

  outputs =
    { self, nixpkgs }:
    let
      supportedSystems = [
        "x86_64-linux"
        "aarch64-linux"
        "x86_64-darwin"
        "aarch64-darwin"
      ];
      forAllSystems = nixpkgs.lib.genAttrs supportedSystems;
      pkgsFor = system: nixpkgs.legacyPackages.${system};
    in
    {
      packages = forAllSystems (
        system:
        let
          pkgs = pkgsFor system;
        in
        {
          default = self.packages.${system}.dmrconfig;
          dmrconfig =
            let
              darwinFrameworks = with pkgs.darwin.apple_sdk.frameworks; [
                IOKit
                CoreFoundation
                Security
              ];
            in
            pkgs.stdenv.mkDerivation (finalAttrs: {
              pname = "dmrconfig";
              version = builtins.substring 0 7 finalAttrs.src.rev or "dev";
              src = self;

              nativeBuildInputs = [ pkgs.pkg-config ];

              buildInputs =
                [ pkgs.libusb1 ]
                ++ pkgs.lib.optionals pkgs.stdenv.isDarwin darwinFrameworks
                ++ pkgs.lib.optionals pkgs.stdenv.isLinux [ pkgs.udev ];

              # serial.c uses kIOMainPortDefault (macOS 12+) behind a
              # __builtin_available guard, but the symbol must still be
              # *declared* at compile time. nixpkgs' default darwin SDK
              # (10.12) only has the deprecated kIOMasterPortDefault.
              # Both resolve to the same default main port at runtime, and
              # the surrounding `#pragma clang diagnostic ignored
              # -Wdeprecated-declarations` suppresses the warning, so this
              # substitution is safe on every macOS version.
              postPatch = pkgs.lib.optionalString pkgs.stdenv.isDarwin ''
                substituteInPlace serial.c --replace-fail \
                  "kIOMainPortDefault" "kIOMasterPortDefault"
              '';

              # The Makefile defaults to `gcc`; use the stdenv compiler.
              makeFlags = [ "CC=${pkgs.stdenv.cc.targetPrefix}cc" ];

              doCheck = false;

              installPhase = ''
                runHook preInstall
                install -Dm755 dmrconfig $out/bin/dmrconfig
                install -Dm644 dmrconfig.1 $out/share/man/man1/dmrconfig.1
                runHook postInstall
              '';

              meta = with pkgs.lib; {
                description = "Configuration utility for DMR radios";
                longDescription = ''
                  DMRconfig is a CLI utility for programming digital radios
                  via USB programming cable. Supports Baofeng DM-32UV,
                  DM-1701, RD-5R, DM-1801; TYT MD-380/390/2017/UV380/UV390/
                  9600; Radioddity GD-77; Anytone D868UV/D878UV/D878UVII;
                  BTECH DMR-6x2; and others.
                '';
                homepage = "https://github.com/OpenRTX/dmrconfig";
                license = licenses.bsd3;
                platforms = supportedSystems;
                mainProgram = "dmrconfig";
              };
            });
        }
      );

      devShells = forAllSystems (
        system:
        let
          pkgs = pkgsFor system;
        in
        {
          default = pkgs.mkShell {
            nativeBuildInputs = [ pkgs.pkg-config ];
            buildInputs =
              [
                pkgs.libusb1
                pkgs.gnumake
              ]
              ++ pkgs.lib.optionals pkgs.stdenv.isDarwin (
                with pkgs.darwin.apple_sdk.frameworks; [
                  IOKit
                  CoreFoundation
                  Security
                ]
              )
              ++ pkgs.lib.optionals pkgs.stdenv.isLinux [ pkgs.udev ];
          };
        }
      );
    };
}
