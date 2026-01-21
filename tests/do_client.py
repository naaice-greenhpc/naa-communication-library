from pathlib import Path
import numpy as np
import subprocess as sp

from tenacity import retry, wait_fixed

# interface = "enp1s0f0np0"
# sp.run(
#     [
#         "sudo",
#         "mlnx_qos",
#         "-i",
#         interface,
#         "-r",
#         ",".join(8 * [str(0)]),
#     ]
# )
# sp.run(
#     [
#         "sudo",
#         "ip",
#         "link",
#         "set",
#         "dev",
#         interface,
#         "mtu",
#         str(5000),
#     ]
# )

def generate_lengths(max: int):
    min = 1
    curr = min
    while curr < max:
        yield int(curr)
        curr = np.round(curr * 1.5)
    yield max

# sp.check_call(["make", "release"])


file = Path("output.csv")
# print path
print(f"Output file: {file}")
file.unlink(True)
file.write_text("byte_length,setup_time,transfer_time\n")

N = 10
# byte_length = [6124254, 9186381, 20669358, 31004037, 46506056, 69759084, 104638626, 156957939, 235436908, 353155362, 529733043, 794599564, 1073741824]
for dma_length in generate_lengths(1024**3):
    print(f"Testing {dma_length}B")
    @retry(wait=wait_fixed(0.5))
    def run():
        sp.check_call(
                ["build/tests/naaice_client_low_level_measurement", "10.3.10.41","10.3.10.42" ,str(dma_length), file]
        )
    for i in range(N):
        print(f"Testing {dma_length}B ({i+1}/{N})")
        run()
