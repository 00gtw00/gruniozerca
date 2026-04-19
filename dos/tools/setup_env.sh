#!/usr/bin/env bash
# =============================================================================
# Gruniożerca DOS — Skrypt konfiguracji środowiska (Linux/macOS)
# Instaluje cross-kompilator DJGPP dla celu i686-pc-msdosdjgpp
# =============================================================================

set -e
DJGPP_VERSION="12.2.0"
INSTALL_DIR="${HOME}/djgpp"

echo "=== Instalacja DJGPP cross-kompilatora dla DOS ==="
echo "Cel: i686-pc-msdosdjgpp-gcc ${DJGPP_VERSION}"
echo "Katalog: ${INSTALL_DIR}"
echo ""

# --------------- Wykrywanie systemu -----------------------------------------
if command -v apt-get &>/dev/null; then
    PKG_MGR="apt"
elif command -v dnf &>/dev/null; then
    PKG_MGR="dnf"
elif command -v pacman &>/dev/null; then
    PKG_MGR="pacman"
elif command -v brew &>/dev/null; then
    PKG_MGR="brew"
else
    echo "BŁĄD: Nieznany menedżer pakietów. Zainstaluj DJGPP ręcznie."
    echo "Pobierz z: https://github.com/andrewwutw/build-djgpp"
    exit 1
fi

# --------------- Zależności budowania ----------------------------------------
echo "[1/4] Instalacja zależności..."
case "$PKG_MGR" in
    apt)
        sudo apt-get update -qq
        sudo apt-get install -y gcc g++ make texinfo bison flex \
            libgmp-dev libmpfr-dev libmpc-dev curl wget
        ;;
    dnf)
        sudo dnf install -y gcc gcc-c++ make texinfo bison flex \
            gmp-devel mpfr-devel libmpc-devel curl wget
        ;;
    pacman)
        sudo pacman -S --noconfirm gcc make texinfo bison flex \
            gmp mpfr libmpc curl wget
        ;;
    brew)
        brew install gcc make texinfo bison flex gmp mpfr libmpc curl wget
        ;;
esac

# --------------- Pobieranie build-djgpp --------------------------------------
echo "[2/4] Pobieranie skryptu build-djgpp..."
TMPDIR=$(mktemp -d)
cd "$TMPDIR"
curl -L https://github.com/andrewwutw/build-djgpp/archive/refs/heads/master.tar.gz \
     -o build-djgpp.tar.gz
tar xzf build-djgpp.tar.gz
cd build-djgpp-master

# --------------- Kompilacja cross-kompilatora --------------------------------
echo "[3/4] Kompilacja DJGPP (może potrwać 15-30 minut)..."
./build-djgpp.sh gcc

# --------------- Konfiguracja PATH -------------------------------------------
echo "[4/4] Konfiguracja zmiennych środowiskowych..."
DJGPP_BIN="${INSTALL_DIR}/bin"
if ! grep -q "djgpp" "${HOME}/.bashrc" 2>/dev/null; then
    cat >> "${HOME}/.bashrc" <<EOF

# DJGPP DOS cross-compiler
export PATH="${DJGPP_BIN}:\$PATH"
export DJGPP_PREFIX="${INSTALL_DIR}"
EOF
fi

echo ""
echo "=== Instalacja zakończona ==="
echo "Uruchom: source ~/.bashrc"
echo "Sprawdź: i686-pc-msdosdjgpp-gcc --version"
echo ""
echo "Aby skompilować grę:"
echo "  cd dos && make all"
echo ""
echo "Opcjonalnie zainstaluj DOSBox-X do testowania:"
echo "  sudo apt install dosbox-x    # Ubuntu/Debian"
echo "  brew install dosbox-x        # macOS"
