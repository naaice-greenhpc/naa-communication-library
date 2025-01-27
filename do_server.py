import subprocess as sp

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
while True:
    sp.check_call(["bin/naaice_server"])
