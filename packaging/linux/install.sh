#!/usr/bin/env bash
set -euo pipefail

INSTALL_DIR="$(cd "$(dirname "$(realpath "$0")")" && pwd)"
NEONIFY_BIN="$INSTALL_DIR/neonify"
NEONIFY_ICON="$INSTALL_DIR/_internal/logo.png"
DESKTOP_FILE="$HOME/.local/share/applications/neonify.desktop"

if [[ ! -f "$NEONIFY_BIN" ]]; then
    echo "ERROR: neonify binary not found at $NEONIFY_BIN"
    exit 1
fi

if [[ ! -f "$NEONIFY_ICON" ]]; then
    echo "ERROR: _internal/logo.png not found at $NEONIFY_ICON"
    exit 1
fi

if [[ ! -x "$NEONIFY_BIN" ]]; then
    echo "Making neonify executable..."
    chmod +x "$NEONIFY_BIN"
fi

echo "Creating desktop shortcut..."
mkdir -p "$HOME/.local/share/applications"

cat > "$DESKTOP_FILE" <<DESKTOP
[Desktop Entry]
Name=Neonify
Comment=Procedural Neon Art Tool
Exec=${NEONIFY_BIN}
Icon=${NEONIFY_ICON}
Terminal=false
Type=Application
Categories=Graphics;
StartupNotify=true
DESKTOP

chmod 644 "$DESKTOP_FILE"

if command -v update-desktop-database &>/dev/null; then
    update-desktop-database "$HOME/.local/share/applications" 2>/dev/null || true
fi

SHELL_NAME=""
SHELL_RC=""

if [[ -n "${ZSH_VERSION:-}" ]] || [[ "$(basename "${SHELL:-}")" == "zsh" ]]; then
    SHELL_NAME="zsh"
    SHELL_RC="$HOME/.zshrc"
elif [[ -n "${BASH_VERSION:-}" ]] || [[ "$(basename "${SHELL:-}")" == "bash" ]]; then
    SHELL_NAME="bash"
    SHELL_RC="$HOME/.bashrc"
fi

if [[ -z "$SHELL_RC" ]]; then
    if [[ -f "$HOME/.zshrc" ]]; then
        SHELL_NAME="zsh"
        SHELL_RC="$HOME/.zshrc"
    elif [[ -f "$HOME/.bashrc" ]]; then
        SHELL_NAME="bash"
        SHELL_RC="$HOME/.bashrc"
    else
        SHELL_NAME="bash"
        SHELL_RC="$HOME/.bashrc"
    fi
fi

ALIAS_MARKER="# >>> Neonify alias >>>"
ALIAS_LINE="alias neonify='${NEONIFY_BIN}'"

if [[ -f "$SHELL_RC" ]]; then
    if grep -qF "$ALIAS_MARKER" "$SHELL_RC"; then
        echo "Shell alias already exists in $SHELL_RC (skipping)"
    else
        echo "Adding alias to $SHELL_RC ($SHELL_NAME)..."
        echo "" >> "$SHELL_RC"
        echo "$ALIAS_MARKER" >> "$SHELL_RC"
        echo "$ALIAS_LINE" >> "$SHELL_RC"
        echo "# <<< Neonify alias <<<" >> "$SHELL_RC"
    fi
else
    echo "Creating $SHELL_RC with alias ($SHELL_NAME)..."
    echo "$ALIAS_MARKER" >> "$SHELL_RC"
    echo "$ALIAS_LINE" >> "$SHELL_RC"
    echo "# <<< Neonify alias <<<" >> "$SHELL_RC"
fi

OTHER_RC=""
if [[ "$SHELL_RC" == "$HOME/.bashrc" ]] && [[ -f "$HOME/.zshrc" ]]; then
    OTHER_RC="$HOME/.zshrc"
elif [[ "$SHELL_RC" == "$HOME/.zshrc" ]] && [[ -f "$HOME/.bashrc" ]]; then
    OTHER_RC="$HOME/.bashrc"
fi

if [[ -n "$OTHER_RC" ]]; then
    if grep -qF "$ALIAS_MARKER" "$OTHER_RC"; then
        echo "Alias already exists in $OTHER_RC (skipping)"
    else
        echo "Also adding alias to $OTHER_RC..."
        echo "" >> "$OTHER_RC"
        echo "$ALIAS_MARKER" >> "$OTHER_RC"
        echo "$ALIAS_LINE" >> "$OTHER_RC"
        echo "# <<< Neonify alias <<<" >> "$OTHER_RC"
    fi
fi

echo ""
echo "============================================================"
echo " Neonify installed successfully!"
echo "============================================================"
echo ""
echo " Desktop shortcut:  $DESKTOP_FILE"
echo "                     Neonify now appears in your app menu"
echo "                     under Graphics."
echo ""
echo " Shell alias:       $ALIAS_LINE"
echo "                     Open a new terminal or run:"
echo "                       source $SHELL_RC"
echo "                     Then use: neonify info"
echo ""
echo "============================================================"
