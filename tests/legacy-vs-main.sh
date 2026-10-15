#!/bin/bash
# Validation de la variante legacy (GCC 4.8.5 / C++11) contre le programme
# principal : les CSV doivent etre identiques byte a byte.
set -e
cd /mnt/c/GIT/size-directory
W=/var/tmp/dslegacy
rm -rf "$W"; mkdir -p "$W"

echo "=== builds ==="
g++ -std=c++11 -O2 -Wall -Wextra dirsize-legacy.cpp -o "$W/legacy" && echo "BUILD legacy OK"
g++ -std=c++20 -O2 dirsize.cpp -o "$W/main" && echo "BUILD main OK"

echo "=== 1) arbre synthetique : symlinks + date 1960 + vide + fichiers racine ==="
mkdir -p "$W/ts/real/deep" "$W/ts/vide"
echo -n "12345"      > "$W/ts/real/a.txt"
echo -n "1234567890" > "$W/ts/real/deep/b.txt"
ln -s ../real "$W/ts/linkdir"
ln -s real/a.txt "$W/ts/linkfile"
ln -s .. "$W/ts/real/deep/loop"
mkdir -p "$W/ts/d"
echo x > "$W/ts/d/old.txt"
touch -d "1960-05-04 12:00:00" "$W/ts/d/old.txt"
echo racine > "$W/ts/racine.txt"
"$W/legacy" "$W/ts" "$W/ts_leg.csv" >/dev/null 2>&1
"$W/main"   "$W/ts" "$W/ts_main.csv" >/dev/null 2>&1
diff "$W/ts_main.csv" "$W/ts_leg.csv" && echo "OK identiques"
cat "$W/ts_leg.csv"

echo "=== 2) /usr : comparaison byte-exact ==="
"$W/legacy" /usr "$W/usr_leg.csv" >/dev/null 2>&1
"$W/main"   /usr "$W/usr_main.csv" >/dev/null 2>&1
diff "$W/usr_main.csv" "$W/usr_leg.csv" && echo "OK /usr identiques"
wc -l "$W/usr_leg.csv"

echo "=== 3) slash final ==="
"$W/legacy" /usr/ "$W/usr_slash.csv" >/dev/null 2>&1
tail -1 "$W/usr_slash.csv"

echo "=== 4) bind mount (root) : pas de boucle, compte une fois ==="
mkdir -p "$W/tb/A" "$W/tb/B"
echo -n "1234567" > "$W/tb/A/f"
mount --bind "$W/tb/A" "$W/tb/B" 2>/dev/null && {
  timeout 10 "$W/legacy" "$W/tb" "$W/tb.csv" >/dev/null 2>&1 && echo "OK sans boucle"
  umount "$W/tb/B"
  cat "$W/tb.csv"
} || echo "SKIP (pas root)"
