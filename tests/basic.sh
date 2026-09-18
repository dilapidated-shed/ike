#!/bin/sh
set -eu

ike_root=$(pwd)
ike_bin=$ike_root/ike
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

runner="$tmp/runner"
cat > "$runner" <<'EOF'
#!/bin/sh
set -eu
[ "$#" -eq 1 ]
printf '%s\n' "$1" > "$IKE_TEST_SOURCE_PATH"
printf 'run\n' >> "$IKE_TEST_RUNNER_LOG"
exec /bin/sh "$1"
EOF
chmod 0755 "$runner"
printf 'runner\n' > runner-input.txt
cp_bin=$(command -v cp)
printf 'runner-output.txt depends on runner-input.txt\n    %s runner-input.txt runner-output.txt\n' \
    "$cp_bin" > Ikefile
IKE_TEST_RUNNER_LOG="$tmp/runner.log" \
IKE_TEST_SOURCE_PATH="$tmp/source-path" \
IKE_RECIPE_RUNNER="$runner" \
    "$ike_bin" runner-output.txt > runner-first.log
[ "$(cat runner-output.txt)" = "runner" ]
[ "$(cat runner.log)" = "run" ]
[ "$(wc -l < runner-first.log | tr -d ' ')" = "1" ]
[ ! -e "$(cat source-path)" ]
IKE_TEST_RUNNER_LOG="$tmp/runner.log" \
IKE_TEST_SOURCE_PATH="$tmp/source-path" \
IKE_RECIPE_RUNNER="$runner" \
    "$ike_bin" runner-output.txt > runner-second.log
[ ! -s runner-second.log ]
[ "$(cat runner.log)" = "run" ]

if IKE_RECIPE_RUNNER=relative-runner "$ike_bin" runner-output.txt \
    > runner-relative.out 2> runner-relative.err; then
    echo "expected relative recipe runner to fail" >&2
    exit 1
fi
grep -Fqx 'ike: IKE_RECIPE_RUNNER must be an absolute path' runner-relative.err


hex_text()
{
    printf '%s' "$1" | od -An -tx1 | tr -d ' \n'
}

printf 'receipt\n' > receipt-input.txt
cat > Ikefile <<'EOF'
receipt-output.txt depends on receipt-input.txt
    cp receipt-input.txt receipt-output.txt
EOF
receipt="$tmp/ike-build-v1.tsv"
IKE_RECEIPT="$receipt" IKE_IKEFILE_IDENTITY='fixture:receipt-v1' \
    "$ike_bin" receipt-output.txt > receipt.out
target_hex=$(hex_text 'receipt-output.txt')
identity_hex=$(hex_text 'fixture:receipt-v1')
runner_hex=$(hex_text 'POSIX-system()')
recipe_hex=$(hex_text 'cp receipt-input.txt receipt-output.txt')
grep -Fqx 'schema	ike-build-v1' "$receipt"
grep -Fqx "selected_target_hex	$target_hex" "$receipt"
grep -Fqx "ikefile_identity_hex	$identity_hex" "$receipt"
grep -Fqx 'recipe_runner_mode	posix-system' "$receipt"
grep -Fqx "recipe_runner_identity_hex	$runner_hex" "$receipt"
grep -Fqx "rule	0	$target_hex" "$receipt"
grep -Fqx "recipe	1	$target_hex	$recipe_hex	0" "$receipt"
grep -Fqx 'final_result	PASS' "$receipt"

cat > Ikefile <<'EOF'
failed-receipt.txt depends on receipt-input.txt
    /bin/sh -c 'exit 7'
EOF
failed_receipt="$tmp/ike-build-v1-failed.tsv"
if IKE_RECEIPT="$failed_receipt" IKE_IKEFILE_IDENTITY='fixture:failed-v1' \
    "$ike_bin" failed-receipt.txt > failed-receipt.out 2> failed-receipt.err; then
    echo "expected failed recipe receipt run to fail" >&2
    exit 1
fi
failed_target_hex=$(hex_text 'failed-receipt.txt')
failed_recipe_hex=$(hex_text "/bin/sh -c 'exit 7'")
grep -Fqx "recipe	1	$failed_target_hex	$failed_recipe_hex	7" "$failed_receipt"
grep -Fqx 'final_result	FAIL' "$failed_receipt"

if IKE_RECEIPT="$tmp/missing-identity.tsv" \
    "$ike_bin" failed-receipt.txt > missing-identity.out 2> missing-identity.err; then
    echo "expected missing receipt identity to fail" >&2
    exit 1
fi
grep -Fqx 'ike: IKE_IKEFILE_IDENTITY is required when IKE_RECEIPT is set' \
    missing-identity.err

fixture="$tmp/aici-fixture"
mkdir -p "$fixture"
cp "$ike_root/tests/aici-fixture/Ikefile" "$fixture/Ikefile"
cp "$ike_root/tests/aici-fixture/verifier.c" "$fixture/verifier.c"
(
    cd "$fixture"
    IKE_RECEIPT="$tmp/aici-fixture-receipt.tsv" \
    IKE_IKEFILE_IDENTITY='fixture:aici-c-v1' \
        "$ike_bin" aici-self-test > "$tmp/aici-fixture.out"
)
grep -Fqx 'aici-fixture-ok' "$tmp/aici-fixture.out"
grep -Fqx 'final_result	PASS' "$tmp/aici-fixture-receipt.tsv"

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
