import subprocess as sp
from pathlib import Path

import numpy as np
from tenacity import retry, wait_fixed

interface = "enp179s0f0np0"
sp.run(
    [
        "sudo",
        "mlnx_qos",
        "-i",
        interface,
        "-r",
        ",".join(8 * [str(0)]),
    ]
)
sp.run(
    [
        "sudo",
        "ip",
        "link",
        "set",
        "dev",
        interface,
        "mtu",
        str(5000),
    ]
)


def generate_lengths(max: int):
    min = 1
    curr = min
    while curr < max:
        yield int(curr)
        curr = np.round(curr * 1.5)
    yield max


sp.check_call(["make", "release"])

file = Path("output.csv")
file.unlink(True)
file.write_text("byte_length,setup_time,transfer_time\n")

N = 10

for dma_length in generate_lengths(1024**3):
    print(f"Testing {dma_length}B")

    @retry(wait=wait_fixed(0.2))
    def run():
        sp.check_call(
            ["bin/naaice_client", "10.42.50.1", "10.42.50.110", str(dma_length), file]
        )

    for i in range(N):
        print(f"Testing {dma_length}B ({i+1}/{N})")
        run()
