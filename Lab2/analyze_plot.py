import pandas as pd
import numpy as np
import matplotlib.pyplot as plt

df = pd.read_csv('data/results.csv')
agg = df.groupby(['N','p']).agg(T_mean=('T','mean'), T_std=('T','std')).reset_index()
# для каждого N вычисляем T1
plots = []
for N, g in agg.groupby('N'):
    T1 = g[g.p==1].T_mean.values[0]
    g = g.copy()
    g['S'] = T1 / g['T_mean']
    g['E'] = g['S'] / g['p']
    # Karp-Flatt / Amdahl's effective serial fraction
    def e_row(row):
        p = row['p']
        S = row['S']
        if p == 1:
            return 0.0
        return (1.0/S - 1.0/p) / (1.0 - 1.0/p)
    g['f'] = g.apply(e_row, axis=1)
    plots.append((N,g))
    # Save table
    g.to_csv(f'summary_N_{int(N)}.csv', index=False)

    # Plots
    plt.figure(figsize=(10,6))
    plt.errorbar(g['p'], g['T_mean'], yerr=g['T_std'], fmt='o-', label='T(p)')
    plt.xlabel('p (threads)')
    plt.ylabel('Time (s)')
    plt.title(f'Time vs p, N={N}')
    plt.grid(True)
    plt.savefig(f'data/T_vs_p_N_{int(N)}.png')

    plt.figure(figsize=(10,6))
    plt.plot(g['p'], g['S'], 'o-', label='Measured S(p)')
    plt.plot(g['p'], g['p'], '--', label='Ideal S=p')
    plt.xlabel('p (threads)')
    plt.ylabel('Speedup S(p)')
    plt.title(f'Speedup vs p, N={N}')
    plt.legend(); plt.grid(True)
    plt.savefig(f'data/S_vs_p_N_{int(N)}.png')

    plt.figure(figsize=(10,6))
    plt.plot(g['p'], g['E'], 'o-')
    plt.xlabel('p')
    plt.ylabel('Efficiency E(p)')
    plt.title(f'Efficiency vs p, N={N}')
    plt.grid(True)
    plt.savefig(f'data/E_vs_p_N_{int(N)}.png')

    plt.figure(figsize=(10,6))
    plt.plot(g['p'], g['f'], 'o-')
    plt.xlabel('p')
    plt.ylabel('Estimated serial fraction f(p)')
    plt.title(f'Amdahl/Karp-Flatt f(p), N={N}')
    plt.grid(True)
    plt.savefig(f'data/f_vs_p_N_{int(N)}.png')

print("Done. Summaries saved.")