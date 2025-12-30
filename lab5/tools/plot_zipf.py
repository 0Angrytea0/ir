import math
import sys
import pandas as pd
import matplotlib.pyplot as plt

def read_zipf(path):
    df = pd.read_csv(path, sep="\t")
    r = df["rank"].to_numpy(dtype=float)
    f = df["frequency"].to_numpy(dtype=float)
    return r, f

def zipf_curve(r, f1, s=1.0):
    c = f1 * (1.0 ** s)
    return c / (r ** s)

def fit_mandelbrot(r, f, k=2000):
    k = min(k, len(r))
    r = r[:k]
    f = f[:k]
    best = None
    for b in [0.0, 0.5, 1.0, 2.0, 5.0, 10.0, 20.0, 50.0]:
        x = [math.log(ri + b) for ri in r]
        y = [math.log(fi) for fi in f]
        n = len(x)
        sx = sum(x); sy = sum(y)
        sxx = sum(v*v for v in x)
        sxy = sum(x[i]*y[i] for i in range(n))
        den = n*sxx - sx*sx
        if den == 0:
            continue
        a1 = (n*sxy - sx*sy) / den
        a0 = (sy - a1*sx) / n
        s = -a1
        c = math.exp(a0)
        pred = [c / ((ri + b) ** s) for ri in r]
        err = sum((pred[i] - f[i])**2 for i in range(n)) / n
        cand = (err, c, b, s)
        if best is None or cand[0] < best[0]:
            best = cand
    return best

def plot_one(in_tsv, out_png, title):
    r, f = read_zipf(in_tsv)
    plt.figure()
    plt.loglog(r, f, label="data")
    plt.loglog(r, zipf_curve(r, f[0], s=1.0), label="Zipf s=1, C=f1")

    best = fit_mandelbrot(r, f, k=2000)
    if best is not None:
        err, c, b, s = best
        plt.loglog(r, c / ((r + b) ** s), label=f"Mandelbrot C={c:.2g}, b={b}, s={s:.3f}")

    plt.title(title)
    plt.xlabel("rank")
    plt.ylabel("frequency")
    plt.legend()
    plt.tight_layout()
    plt.savefig(out_png, dpi=150)

if __name__ == "__main__":
    plot_one(r".\out\zipf_raw.tsv",  r".\out\zipf_raw.png",  r".\out\zipf_raw.tsv")
    plot_one(r".\out\zipf_stem.tsv", r".\out\zipf_stem.png", r".\out\zipf_stem.tsv")
