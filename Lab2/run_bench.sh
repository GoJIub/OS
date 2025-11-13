#!/bin/bash
EXEC=./dice_sim.out
K=10; cur=1; A=0; B=0
Ns=(10000000 100000000)
Ps=(1 2 4 6 8)
R=15
OUT=data/results.csv

# header
echo "N,p,run,T" > "$OUT"

for N in "${Ns[@]}"; do
  for p in "${Ps[@]}"; do
    for run in $(seq 1 $R); do
      echo "Running N=$N, p=$p, run=$run ..."
      # перенаправляем stderr в stdout и ищем строку с "Elapsed time"
      T=$($EXEC "$K" "$cur" "$A" "$B" "$N" "$p" 2>&1 \
          | grep "Elapsed time:" \
          | awk '{print $(NF-1)}')

      if [[ -z "$T" ]]; then
        echo "Warning: no Elapsed time found for N=$N, p=$p, run=$run" >&2
        T="NaN"
      fi

      echo "$N,$p,$run,$T" >> "$OUT"
      sleep 0.5
    done
  done
done