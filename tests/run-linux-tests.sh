#!/bin/bash
# Tests de non-regression Linux (a lancer depuis WSL ou Linux) :
#   bash tests/run-linux-tests.sh
# Verifie : symlinks ignores, dates < 1970, bind mount sans boucle ni double
# comptage, coherence des sorties. Requiert g++ et (pour le test bind mount)
# les droits root ; sans root, ce test est saute.
set -e
cd "$(dirname "$0")/.."
W=$(mktemp -d /var/tmp/dirsize-tests.XXXXXX)
trap 'rm -rf "$W"' EXIT
PASS=0; FAIL=0
check() { if [ "$1" = "$2" ]; then echo "PASS: $3"; PASS=$((PASS+1)); else echo "FAIL: $3 (attendu [$2], obtenu [$1])"; FAIL=$((FAIL+1)); fi }

g++ -std=c++17 -O2 -Wall -Wextra dirsize.cpp -o "$W/ds17" -lstdc++fs
g++ -std=c++20 -O2 -Wall -Wextra dirsize.cpp -o "$W/ds20"

# --- symlinks : repertoire non suivi, fichier non compte, cycle sans boucle
mkdir -p "$W/ts/real/deep"
echo -n "12345"      > "$W/ts/real/a.txt"
echo -n "1234567890" > "$W/ts/real/deep/b.txt"
ln -s ../real    "$W/ts/linkdir"
ln -s real/a.txt "$W/ts/linkfile"
ln -s ..         "$W/ts/real/deep/loop"
timeout 10 "$W/ds20" "$W/ts" "$W/ts.csv" >/dev/null 2>&1
check "$(grep -c '^"real";15;2;' "$W/ts.csv")" 1 "symlink dir/file ignores"
check "$(grep -c 'linkdir\|linkfile' "$W/ts.csv")" 0 "aucune ligne pour les liens"

# --- date avant 1970
mkdir -p "$W/t60/d"; echo x > "$W/t60/r"; touch -d "1960-05-04 12:00:00" "$W/t60/d/f"
"$W/ds20" "$W/t60" "$W/t60.csv" >/dev/null 2>&1
check "$(sed -n 2p "$W/t60.csv" | tr -d '\r')" '"d";0;1;1960-05-04 12:00:00;1960-05-04 12:00:00' "date pre-1970 exacte"

# --- bind mount : compte une seule fois, pas de boucle
if [ "$(id -u)" = 0 ]; then
    mkdir -p "$W/tb_mnt"
    if mount --bind "$W/t60" "$W/tb_mnt" 2>/dev/null; then
        timeout 10 "$W/ds20" "$W/tb_mnt" "$W/tb.csv" >/dev/null 2>&1
        check "$(grep -c 'TOTAL' "$W/tb.csv")" 1 "bind mount root scannable sans boucle"
        umount "$W/tb_mnt"
    else
        echo "SKIP: test bind mount (montage impossible)"
    fi
else
    echo "SKIP: test bind mount (root requis)"
fi

# --- coherence C++17 / C++20 sur un arbre reel
"$W/ds17" /usr "$W/u17.csv" >/dev/null 2>&1
"$W/ds20" /usr "$W/u20.csv" >/dev/null 2>&1
if diff -q "$W/u17.csv" "$W/u20.csv" >/dev/null; then echo "PASS: /usr identique c++17/c++20"; PASS=$((PASS+1)); else echo "FAIL: /usr differe entre c++17 et c++20"; FAIL=$((FAIL+1)); fi

echo "----------------------------------------"
echo "Resultat : $PASS pass, $FAIL fail"
[ "$FAIL" -eq 0 ]
