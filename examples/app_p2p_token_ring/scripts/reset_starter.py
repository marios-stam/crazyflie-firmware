import time
from cflib.utils.power_switch import PowerSwitch
from colorama import init,Fore, Back, Style
from reset_all import reset_cf


uris = [
    'radio://0/20/2M/E7E7E7E700',
    # 'radio://0/20/2M/E7E7E7E701',
    # 'radio://0/20/2M/E7E7E7E702',
    # 'radio://0/20/2M/E7E7E7E703',
    # 'radio://0/20/2M/E7E7E7E704',
    # 'radio://0/20/2M/E7E7E7E705',
    # 'radio://0/20/2M/E7E7E7E706',
    # 'radio://0/20/2M/E7E7E7E707',
    # 'radio://0/20/2M/E7E7E7E708',
    # 'radio://0/20/2M/E7E7E7E709'
]

for uri in uris:
    reset_cf(uri)
