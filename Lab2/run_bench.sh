#!/bin/bash
EXEC=./dice_sim.out
K=10; cur=1; A=0; B=0
Ns=(10000000 100000000)
Ps=(1 2 4 6 8)
R=5
OUT=data/results.csv

# header
echo "N,p,run,T" > $OUT

for N in "${Ns[@]}"; do
  for p in "${Ps[@]}"; do
    for run in $(seq 1 $R); do
      # запускаем и парсим строку "Elapsed time: X s"
      line=$($EXEC $K $cur $A $B $N $p | tail -n 1)
      # предполагаем, что последняя строка содержит 'Elapsed time: X s'
      T=$(echo "$line" | awk '{print $(NF-1)}') # second to last token
      echo "$N,$p,$run,$T" >> $OUT
      sleep 0.5
    done
  done
done