import subprocess as sp
from tenacity import retry, wait_fixed

# interface = "enp179s0f0np0"
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

count_runs = 0
while count_runs < 1040:
    
    @retry(wait=wait_fixed(4))
    def run():
        ret = sp.check_call(
            ["build/tests/naaice_server_measurement"]
        )
        return ret

    ret = run()
    print(f"Testing {count_runs} ({count_runs+1})")
    if ret == 0:
        count_runs += 1
        print(f"Run {count_runs} finished successfully")
    else:   
        print("Error on communication\n")