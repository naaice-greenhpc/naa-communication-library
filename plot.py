#!/usr/bin/env python3
from pathlib import Path
from typing import TypedDict

import matplotlib.pyplot as plt
import pandas as pd
from matplotlib import ticker
from matplotlib.axes import Axes


class PlotData(TypedDict):
    byte_length: float
    setup_time: float
    transfer_time: float


def plot(
    data: pd.DataFrame | list[PlotData], name: str, write_csv: bool = False
) -> Path:
    """
    :param data: DataFrame with columns `byte_length`, `setup_time` and `transfer_time` or a list of PlotData
    """
    if isinstance(data, pd.DataFrame):
        data_frame = data
    else:
        data_frame = pd.DataFrame.from_records(data)

    data_frame["throughput"] = data_frame["byte_length"] * 8 * 2 / data_frame["transfer_time"]
    grouped = data_frame.groupby(["byte_length"])

    fig, axes = plt.subplots(2, 1, sharex=True, figsize=(10, 10))
    formatter_byte = ticker.EngFormatter("B", places=1)
    formatter_time = ticker.EngFormatter("s", places=1)
    formatter_bps = ticker.EngFormatter("bps", places=1)
    ax1: Axes = axes[0]
    ax2: Axes = axes[1]

    mean = grouped.mean()
    # ddof = 0 is needed to avoid NaN when a `byte_length` has only one value
    std_dev = grouped.std(ddof=0)

    for column in ["setup_time", "transfer_time"]:
        ax1.loglog(mean.index.values, mean[column].values, label=column)  # type: ignore
        ax1.fill_between(
            mean.index.values,
            (mean - std_dev)[column].values,  # type: ignore
            (mean + std_dev)[column].values,  # type: ignore
            alpha=0.5,
        )
    ax1.set_xscale("log", base=2)  # type: ignore
    ax1.xaxis.set_major_formatter(formatter_byte)
    ax1.yaxis.set_major_formatter(formatter_time)
    ax1.tick_params(axis="x", labelrotation=45, labelbottom=True)
    ax1.grid()
    ax1.set_ylabel("Time")
    ax1.legend()
    # ax1.set_title(f"Latency: {formatter_time(mean["column"].min())}")

    ax2.semilogx(mean.index.values, mean["throughput"].values)  # type: ignore
    ax2.fill_between(
        mean.index.values,
        (mean - std_dev)["throughput"].values,  # type: ignore
        (mean + std_dev)["throughput"].values,  # type: ignore
        alpha=0.5,
    )
    ax2.set_xscale("log", base=2)  # type: ignore
    ax2.xaxis.set_major_formatter(formatter_byte)
    ax2.yaxis.set_major_locator(ticker.MultipleLocator(1000**3 * 10))
    ax2.yaxis.set_major_formatter(formatter_bps)
    ax2.tick_params(axis="x", labelrotation=45, labelbottom=True)
    ax2.grid()
    ax2.set_ylabel("Throughput")
    ax2.set_xlabel("Payload length")
    ax2.set_title(f"Maximum: {formatter_bps(mean['throughput'].max())}")

    plt.tight_layout()
    result_path = Path(__file__).parent / "results"
    result_path.mkdir(exist_ok=True)
    plt.savefig(result_path / f"{name}.pdf")
    plt.savefig(result_path / f"{name}.svg")
    if write_csv:
        data_frame.to_csv(result_path / f"{name}.csv")
    return result_path / f"{name}.svg"


if __name__ == "__main__":
    for dataset in ["fpga", "server"]:
        data = pd.read_csv(Path(__file__).parent / f"{dataset}.csv")
        plot(data, dataset)
