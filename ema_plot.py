#!/usr/bin/env python3
import tarfile
from pathlib import Path
from typing import Any, TypedDict

import matplotlib.pyplot as plt
import pandas as pd
from matplotlib import ticker

def formatter_byte(num: float, pos: None=None):
    for unit in ["B", "kiB", "MiB", "GiB"]:
        if num < 1024:
            return f"{num:g} {unit}"
        num /= 1024
    return f"{num:g} TiB"

def plot(data: pd.DataFrame):
    grouped = data.groupby(["byte_length"])

    _, (ax1, ax2) = plt.subplots(1, 2, sharex=True, figsize=(10, 5))
    formatter_time = ticker.EngFormatter("s", places=1)
    formatter_energy = ticker.EngFormatter("J", places=1)

    mean = grouped.mean()
    # ddof = 0 is needed to avoid NaN when a `byte_length` has only one value
    std_dev = grouped.std(ddof=0)

    for column, label in [
        ("native_rpc_time", "Native RPC"),
        ("hls_rpc_time", "HLS RPC"),
        ("local_time", "CPU Addition"),
        ]:
        ax1.plot(mean.index.values, mean[column].values, label=label)  # type: ignore
        ax1.fill_between(
            mean.index.values,
            (mean - std_dev)[column].values,  # type: ignore
            (mean + std_dev)[column].values,  # type: ignore
            alpha=0.5,
        )
    # ax1.set_xscale("log", base=2)  # type: ignore
    ax1.xaxis.set_major_formatter(formatter_byte)
    ax1.yaxis.set_major_formatter(formatter_time)
    ax1.tick_params(axis="x", labelrotation=45, labelbottom=True)
    ax1.grid()
    ax1.set_ylabel("VectorAdd Time")
    ax1.set_xlabel("Size of one Memory Region")
    ax1.legend()
    
    for column, label in [
        ("native_rpc_energy", "Native RPC"),
        ("hls_rpc_energy", "HLS RPC"),
        ("local_energy_cpu", "CPU Addition"),
        ("native_rpc_energy_fpga", "Native RPC (only FPGA)"),
        ("hls_rpc_energy_fpga", "HLS RPC (only FPGA)"),
        ]:
        ax2.plot(mean.index.values, mean[column].values, label=label)  # type: ignore
        ax2.fill_between(
            mean.index.values,
            (mean - std_dev)[column].values,  # type: ignore
            (mean + std_dev)[column].values,  # type: ignore
            alpha=0.5,
        )
    # ax2.set_xscale("log", base=2)  # type: ignore
    ax2.xaxis.set_major_formatter(formatter_byte)
    ax2.yaxis.set_major_formatter(formatter_energy)
    ax2.tick_params(axis="x", labelrotation=45, labelbottom=True)
    ax2.grid()
    ax2.set_ylabel("VectorAdd Energy")
    ax2.set_xlabel("Size of one Memory Region")
    ax2.legend()

    plt.tight_layout()
    result_path = Path(__file__).parent / "results"
    result_path.mkdir(exist_ok=True)
    plt.savefig(result_path / f"linear.pdf")
    plt.savefig(result_path / f"linear.svg")


if __name__ == "__main__":
    if True:
        tar = tarfile.open("ema_results.tar.gz")
        content = tar.getmembers()
        csvs = [f for f in content if f.name.endswith(".csv")]

        def get_df(csv: tarfile.TarInfo):
            file = tar.extractfile(csv)
            assert file is not None, f"{csv} is None"
            df = pd.read_csv(file)
            csv_dir = "/".join(csv.name.split("/")[:-1])

            def get_energy(region: str, fpga: bool, row: pd.Series):
                ema_file = tar.extractfile(f"{csv_dir}/{row["ema_file"]}")
                assert ema_file is not None, f"EMA file for {row} of {csv} not found"
                ema_df = pd.read_csv(ema_file)
                device_name = "glum_race" if fpga else "CPU"
                return ema_df[
                    (ema_df["region_idf"] == region)
                    & (ema_df["device_name"].str.startswith(device_name))
                ]["energy"].sum()

            for region in ["rpc", "local"]:
                df[f"{region}_energy_fpga"] = df.apply(
                    lambda x: get_energy(region, True, x), axis=1
                )
                df[f"{region}_energy_cpu"] = df.apply(
                    lambda x: get_energy(region, False, x), axis=1
                )
            df["rpc_time"] = df["rpc_time"] / df["n_rpc_invokes"]
            df["rpc_energy_fpga"] = df["rpc_energy_fpga"] / df["n_rpc_invokes"] / 1.0e6
            df["rpc_energy_cpu"] = df["rpc_energy_cpu"] / df["n_rpc_invokes"] / 1.0e6
            df["local_time"] = (df["local_time"] / df["n_rpc_invokes"]).clip(lower=1e-6)
            df["local_energy_cpu"] = (df["local_energy_cpu"] / df["n_rpc_invokes"] / 1.0e6).clip()
            return df

        dfs = {csv.name: get_df(csv) for csv in csvs}
        merged_df = pd.DataFrame()
        for col in ["byte_length", "local_time", "local_energy_cpu"]:
            merged_df[col] = dfs["results_local/vec_add_local.csv"][col]
        for role in ["native", "hls"]:
            for col in ["rpc_time", "rpc_energy_fpga", "rpc_energy_cpu"]:
                merged_df[f"{role}_{col}"] = dfs[f"results_{role}/vec_add_{role}.csv"][col]
            merged_df[f"{role}_rpc_energy"] = merged_df[f"{role}_rpc_energy_fpga"] + merged_df[f"{role}_rpc_energy_cpu"]
        merged_df.to_csv("dataframe.csv")
    else:
        merged_df = pd.read_csv("dataframe.csv")
    print(merged_df)
    plot(merged_df)
