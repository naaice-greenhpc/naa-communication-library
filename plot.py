#!/usr/bin/env python3
from pathlib import Path

import matplotlib.pyplot as plt
import pandas as pd
from matplotlib import ticker

def formatter_byte(num: float, pos: None=None):
    for unit in ["B", "kiB", "MiB", "GiB"]:
        if num < 1024:
            return f"{num:g} {unit}"
        num /= 1024
    return f"{num:g} TiB"

def plot(host2naa: pd.DataFrame, host2host: pd.DataFrame):
    """
    :param data: DataFrame with columns `byte_length`, `setup_time` and `transfer_time` or a list of PlotData
    """
    data_frame = host2naa[["byte_length"]].copy()
    for c in host2naa:
        if c != "byte_length":
            data_frame[f"h2n_{c}"] = host2naa[c]
    for c in host2host:
        if c != "byte_length":
            data_frame[f"h2h_{c}"] = host2host[c]

    data_frame["h2n_throughput"] = (
        data_frame["byte_length"] * 8 * 2 / data_frame["h2n_transfer_time"]
    )
    data_frame["h2h_throughput"] = (
        data_frame["byte_length"] * 8 * 2 / data_frame["h2h_transfer_time"]
    )
    grouped = data_frame.groupby(["byte_length"])

    fig, (ax1, ax2, ax3) = plt.subplots(1, 3, figsize=(15, 5))
    # formatter_byte = ticker.EngFormatter("B", places=1)
    formatter_time = ticker.EngFormatter("s", places=0)
    formatter_bps = ticker.EngFormatter("bps", places=0)

    mean = grouped.mean()
    # ddof = 0 is needed to avoid NaN when a `byte_length` has only one value
    std_dev = grouped.std(ddof=0)

    # ax1: setup time loglog

    for column, label in [("h2n_setup_time", "host2naa"), ("h2h_setup_time", "host2host")]:
        ax1.loglog(mean.index.values, mean[column].values, label=label)  # type: ignore
        ax1.fill_between(
            mean.index.values,
            (mean - std_dev)[column].values,  # type: ignore
            (mean + std_dev)[column].values,  # type: ignore
            alpha=0.5,
        )
    ax1.set_xscale("log", base=2)  # type: ignore
    ax1.xaxis.set_major_locator(ticker.LogLocator(base=2, numticks=17))
    ax1.xaxis.set_major_formatter(formatter_byte)
    ax1.yaxis.set_major_formatter(formatter_time)
    ax1.tick_params(axis="x", labelrotation=45, labelbottom=True)
    ax1.grid()
    ax1.set_ylabel("Time for MRSP")
    ax1.set_xlabel("Memory Region Size")
    ax1.legend()

    for column, label in [("h2n_throughput", "host2naa"), ("h2h_throughput", "host2host")]:
        ax2.semilogx(mean.index.values, mean[column].values, label=label)  # type: ignore
        ax2.fill_between(
            mean.index.values,
            (mean - std_dev)[column].values,  # type: ignore
            (mean + std_dev)[column].values,  # type: ignore
            alpha=0.5,
        )
    ax2.set_xscale("log", base=2)  # type: ignore
    ax2.xaxis.set_major_formatter(formatter_byte)
    ax2.xaxis.set_major_locator(ticker.LogLocator(base=2, numticks=17))
    ax2.yaxis.set_major_formatter(formatter_bps)
    ax2.tick_params(axis="x", labelrotation=45, labelbottom=True)
    ax2.grid()
    ax2.set_ylabel("Bandwidth")
    ax2.set_xlabel("Memory Region Size")
    ax2.legend()

    for column, label in [("h2n_transfer_time", "host2naa"), ("h2h_transfer_time", "host2host")]:
        ax3.loglog(mean.index.values, mean[column].values, label=label)  # type: ignore
        ax3.fill_between(
            mean.index.values,
            (mean - std_dev)[column].values,  # type: ignore
            (mean + std_dev)[column].values,  # type: ignore
            alpha=0.5,
        )
    ax3.set_xscale("log", base=2)  # type: ignore
    ax3.xaxis.set_major_formatter(formatter_byte)
    ax3.xaxis.set_major_locator(ticker.LogLocator(base=2, numticks=17))
    ax3.yaxis.set_major_formatter(formatter_time)
    ax3.tick_params(axis="x", labelrotation=45, labelbottom=True)
    ax3.grid()
    ax3.set_ylabel("Latency")
    ax3.set_xlabel("Memory Region Size")
    ax3.legend()

    plt.tight_layout()
    result_path = Path(__file__).parent / "results"
    result_path.mkdir(exist_ok=True)
    plt.savefig(result_path / f"merged.pdf")
    plt.savefig(result_path / f"merged.svg")


if __name__ == "__main__":
    host2naa, host2host = [
        pd.read_csv(Path(__file__).parent / f"{dataset}.csv")
        for dataset in ["fpga", "server"]
    ]
    plot(host2naa, host2host)
