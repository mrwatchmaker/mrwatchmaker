#!/bin/bash
# MSYS2 MinGW: mrwatchmaker.exe 의 전체 의존 DLL을 mingw64/bin 에서만 재귀 복사

DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$DIR/.." && pwd)"
DEST="${1:-$ROOT}"
EXE="$ROOT/mrwatchmaker.exe"
MINGW_BIN="/mingw64/bin"
export PATH="$MINGW_BIN:/usr/bin:$PATH"

if [[ ! -f "$EXE" ]]; then
  echo "먼저 make 로 mrwatchmaker.exe 를 빌드하세요."
  exit 1
fi

echo "기존 MinGW DLL 정리: $DEST"
find "$DEST" -maxdepth 1 -name '*.dll' -type f -delete 2>/dev/null || true

collect_deps_list() {
  local -a queue=("$@")
  local -A seen=()
  local bin dep
  while ((${#queue[@]} > 0)); do
    bin="${queue[0]}"
    queue=("${queue[@]:1}")
    [[ -f "$bin" ]] || continue
    [[ -n "${seen[$bin]:-}" ]] && continue
    seen[$bin]=1
    while IFS= read -r dep; do
      [[ -f "$dep" ]] || continue
      [[ "$dep" == "$MINGW_BIN"/* ]] || continue
      queue+=("$dep")
    done < <(ldd "$bin" 2>/dev/null | awk '/=> \/mingw64\// {print $3}')
  done
  printf '%s\n' "${!seen[@]}"
}

echo "의존 DLL 수집 중…"
mapfile -t ALL_DEPS < <(collect_deps_list "$EXE")
echo "복사할 DLL: ${#ALL_DEPS[@]}개"
for dep in "${ALL_DEPS[@]}"; do
  [[ "$dep" == *.dll ]] || continue
  cp -f "$dep" "$DEST/"
done

dll_count=$(ls -1 "$DEST"/*.dll 2>/dev/null | wc -l)
echo "완료: $DEST (${dll_count}개 DLL)"
