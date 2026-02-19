#%% imports
from pathlib import Path
import pandas as pd
import matplotlib.pyplot as plt
import numpy as np


measurements_low_level = pd.read_csv('output_low_level.csv')
measurements_middleware = pd.read_csv('output_middleware.csv')
reference = pd.read_csv('tests/fpga_reference.csv')

measurements_low_level_mean = measurements_low_level.groupby('byte_length').mean()
measurements_middleware_mean = measurements_middleware.groupby('byte_length').mean()
reference_mean = reference.groupby('byte_length').mean()

diff_low_level = np.abs(reference_mean['transfer_time'] - measurements_low_level_mean['transfer_time'])
diff_middleware = np.abs(reference_mean['transfer_time'] - measurements_middleware_mean['transfer_time'])

deviation_low_level = diff_low_level / reference_mean['transfer_time']
deviation_middleware = diff_middleware / reference_mean['transfer_time']

plt.plot(reference_mean.index, reference_mean['transfer_time'], label='reference')
plt.plot(measurements_low_level_mean.index, measurements_low_level_mean['transfer_time'], label='measurements low level')
plt.plot(measurements_middleware_mean.index, measurements_middleware_mean['transfer_time'], label='measurements middleware')

plt.yscale('log')
plt.xscale('log')
plt.legend(['reference', 'measurements low level', 'measurements middleware'])
plt.xlabel('Bytes')
plt.ylabel('Latency (s)')
plt.savefig('reference_vs_measurements.pdf', dpi=300)

deviation_treshold = 0.7
if(deviation_low_level.mean() > deviation_treshold or deviation_middleware.mean() > deviation_treshold):
    raise ValueError(f'Deviation (Low-Level API) is {deviation_low_level.mean():.4f} and for the middleware {deviation_middleware.mean():.4f} , which is greater than {deviation_treshold}. See plot in pipeline artificats for details.\n')
else:
    print('Mean deviation (Low-Level API) is:', deviation_low_level.mean(), "\n")
    print('Mean deviation (Middleware API) is:', deviation_middleware.mean(), "\n")

