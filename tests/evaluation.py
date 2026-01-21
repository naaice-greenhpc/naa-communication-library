#%% imports
from pathlib import Path
import pandas as pd
import matplotlib.pyplot as plt
import numpy as np

#%% read the csv file
measurements = pd.read_csv('output.csv')
reference = pd.read_csv('tests/fpga_reference.csv')

measurements_mean = measurements.groupby('byte_length').mean()
reference_mean = reference.groupby('byte_length').mean()

diff = np.abs(reference_mean['transfer_time'] - measurements_mean['transfer_time'])

deviation = diff / reference_mean['transfer_time']


plt.plot(reference_mean.index, reference_mean['transfer_time'], label='reference')
plt.plot(measurements_mean.index, measurements_mean['transfer_time'], label='measurements')
plt.yscale('log')
plt.xscale('log')
plt.legend(['reference', 'measurements'])
plt.xlabel('Bytes')
plt.ylabel('Latency (s)')
plt.savefig('reference_vs_measurements.pdf', dpi=300)

deviation_treshold = 0.5
if(deviation.mean() > deviation_treshold):
    raise ValueError(f'Deviation is {deviation.mean():.4f}, which is greater than {deviation_treshold}. See plot in pipeline artificats for details.\n')
else:
    print('Mean deviation is:', deviation.mean())
