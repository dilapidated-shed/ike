#!/bin/sh
set -eu

ike_bin=$(pwd)/ike
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT HUP INT TERM

cd "$tmp"
printf 'one\n' > input.txt
cat > Ikefile <<'EOF'
output.txt depends on input.txt
    cp input.txt output.txt

final.txt depends on output.txt
    tr a-z A-Z < output.txt > final.txt
EOF

"$ike_bin" final.txt > first.log
[ "$(cat final.txt)" = "ONE" ]
[ "$(wc -l < first.log | tr -d ' ')" = "2" ]

"$ike_bin" final.txt > second.log
[ ! -s second.log ]

sleep 1
printf 'two\n' > input.txt
"$ike_bin" final.txt > third.log
[ "$(cat final.txt)" = "TWO" ]
[ "$(wc -l < third.log | tr -d ' ')" = "2" ]

cat > Ikefile <<'EOF'
broken depends on nowhere
    touch broken
EOF
if "$ike_bin" broken >/dev/null 2>&1; then
    echo "expected missing dependency to fail" >&2
    exit 1
fi

cat > Ikefile <<'EOF'
a depends on b
    touch a
b depends on a
    touch b
EOF
if "$ike_bin" a >/dev/null 2>&1; then
    echo "expected dependency cycle to fail" >&2
    exit 1
fi

printf 'ok\n'
