#!/bin/bash
# run_tests.sh - Lance tous les tests et affiche un rapport
# Usage: ./run_tests.sh [--asm] pour aussi assembler et exécuter les .asm

set -uo pipefail

COMPILER="./bin/tpcc"
TEST_DIR="./test"
RUN_ASM=0
[[ "${1:-}" == "--asm" ]] && RUN_ASM=1

# Couleurs
RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'; NC='\033[0m'

pass=0; fail=0

run_test() {
    local file="$1"
    local expected_ret="$2"
    local category="$3"
    local name
    name=$(basename "$file")

    # Capturer le code de retour et les messages d'erreur
    local actual_ret
    local stderr_out
    stderr_out=$($COMPILER "$file" 2>&1 1>/dev/null; true)
    $COMPILER "$file" >/dev/null 2>&1; actual_ret=$?

    if [[ $actual_ret -eq $expected_ret ]]; then
        echo -e "  ${GREEN}PASS${NC} [$category] $name (code=$actual_ret)"
        ((pass++))
        # Tester l'assemblage si demandé et si good
        if [[ $RUN_ASM -eq 1 && $expected_ret -eq 0 ]]; then
            local asm_file="${file%.tpc}.asm"
            if [[ -f "$asm_file" ]]; then
                local obj_file="${asm_file%.asm}.o"
                local bin_file="${asm_file%.asm}.out"
                if nasm -f elf64 "$asm_file" -o "$obj_file" 2>/dev/null && \
                   ld -o "$bin_file" "$obj_file" 2>/dev/null; then
                    echo -e "    ${GREEN}ASM OK${NC} → $bin_file"
                else
                    echo -e "    ${YELLOW}ASM FAIL${NC} $asm_file"
                fi
                rm -f "$obj_file" "$bin_file"
            fi
        fi
    else
        echo -e "  ${RED}FAIL${NC} [$category] $name (attendu=$expected_ret, obtenu=$actual_ret)"
        if [[ -n "$stderr_out" ]]; then echo "       $stderr_out"; fi
        ((fail++))
    fi
}

echo "=============================="
echo " Tests du compilateur tpcc"
echo "=============================="

# Vérifier que le compilateur existe
if [[ ! -x "$COMPILER" ]]; then
    echo -e "${RED}Erreur: $COMPILER introuvable. Lancez 'make' d'abord.${NC}"
    exit 3
fi

# --- good: retour attendu 0 ---
echo ""
echo "--- Programmes corrects (code de retour attendu: 0) ---"
for f in "$TEST_DIR"/good/*.tpc; do
    [[ -f "$f" ]] && run_test "$f" 0 "good"
done

# --- syn-err: retour attendu 1 ---
echo ""
echo "--- Erreurs lexicales/syntaxiques (code de retour attendu: 1) ---"
for f in "$TEST_DIR"/syn-err/*.tpc; do
    [[ -f "$f" ]] && run_test "$f" 1 "syn-err"
done

# --- sem-err: retour attendu 2 ---
echo ""
echo "--- Erreurs sémantiques (code de retour attendu: 2) ---"
for f in "$TEST_DIR"/sem-err/*.tpc; do
    [[ -f "$f" ]] && run_test "$f" 2 "sem-err"
done

# --- warn: retour attendu 0 (warnings n'empêchent pas la compilation) ---
echo ""
echo "--- Programmes avec avertissements (code de retour attendu: 0) ---"
for f in "$TEST_DIR"/warn/*.tpc; do
    [[ -f "$f" ]] && run_test "$f" 0 "warn"
done

# --- Rapport final ---
total=$((pass + fail))
echo ""
echo "=============================="
echo " Résultats finaux"
echo "=============================="
echo -e " PASS  : ${GREEN}$pass${NC} / $total"
echo -e " FAIL  : ${RED}$fail${NC} / $total"
if [[ $total -gt 0 ]]; then
    score=$(( pass * 100 / total ))
    echo " Score : $score%"
fi
echo "=============================="

# Nettoyer les .asm générés dans test/
rm -f "$TEST_DIR"/good/*.asm "$TEST_DIR"/warn/*.asm 2>/dev/null || true

[[ $fail -eq 0 ]] && exit 0 || exit 1
