#!/usr/bin/env python3

import os
import numpy as np
import matplotlib.pyplot as plt

if __name__ == '__main__':
	fname = os.sys.argv[1]
	print(f"Plotting samples from '{fname}'")
	data = np.fromfile(fname, dtype=np.complex64)
	fig, ax = plt.subplots(2,2)
	ax[0,0].psd(data,NFFT=4096)
	ax[0,0].set_title('PSD (NFFT=4096)')
	ax[1,0].plot(np.real(data))
	ax[1,0].set_title('real(x)')
	ax[1,1].plot(np.imag(data))
	ax[1,1].set_title('imag(x)')
	ax[0,1].plot(np.abs(data))
	ax[0,1].set_title('abs(x)')
	plt.show()
