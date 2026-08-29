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

printf 'new\n' > precise-input.txt
printf 'old\n' > precise-output.txt
cat > Ikefile <<'EOF'
precise-output.txt depends on precise-input.txt
    cp precise-input.txt precise-output.txt
EOF
touch -d '@100.100000000' precise-output.txt
touch -d '@100.200000000' precise-input.txt
"$ike_bin" precise-output.txt > precise.log
[ "$(cat precise-output.txt)" = "new" ]
[ "$(wc -l < precise.log | tr -d ' ')" = "1" ]

printf 'leaf\n' > diamond-source.txt
cat > Ikefile <<'EOF'
diamond-final.txt depends on diamond-left.txt diamond-right.txt
    printf 'final\n' >> diamond-order.log; cat diamond-left.txt diamond-right.txt > diamond-final.txt

diamond-right.txt depends on diamond-leaf.txt
    printf 'right\n' >> diamond-order.log; cp diamond-leaf.txt diamond-right.txt

diamond-left.txt depends on diamond-leaf.txt
    printf 'left\n' >> diamond-order.log; cp diamond-leaf.txt diamond-left.txt

diamond-leaf.txt depends on diamond-source.txt
    printf 'leaf\n' >> diamond-order.log; cp diamond-source.txt diamond-leaf.txt
EOF
"$ike_bin" diamond-final.txt > diamond-first.log
[ "$(cat diamond-order.log)" = "$(printf 'leaf\nleft\nright\nfinal\n')" ]
[ "$(wc -l < diamond-first.log | tr -d ' ')" = "4" ]
"$ike_bin" diamond-final.txt > diamond-second.log
[ ! -s diamond-second.log ]
[ "$(cat diamond-order.log)" = "$(printf 'leaf\nleft\nright\nfinal\n')" ]

cat > Ikefile <<'EOF'
artifact depends upon source
    touch artifact
EOF
if "$ike_bin" artifact > literal.out 2> literal.err; then
    echo "expected near-English dependency phrase to fail" >&2
    exit 1
fi
[ ! -e artifact ]
grep -Fqx "ike: Ikefile:1: expected 'TARGET depends on DEPENDENCIES'" literal.err

cat > Ikefile <<'EOF'
broken depends on nowhere
    touch broken
EOF
if "$ike_bin" broken > missing.out 2> missing.err; then
    echo "expected missing dependency to fail" >&2
    exit 1
fi
[ ! -e broken ]
grep -Fqx "ike: missing dependency 'nowhere' for 'broken'" missing.err

cat > Ikefile <<'EOF'
a depends on b
    touch a
b depends on a
    touch b
EOF
if "$ike_bin" a > cycle.out 2> cycle.err; then
    echo "expected dependency cycle to fail" >&2
    exit 1
fi
[ ! -e a ]
[ ! -e b ]
grep -Fqx "ike: dependency cycle at a" cycle.err

printf 'ok\n'
