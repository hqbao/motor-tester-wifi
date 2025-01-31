import math
import time
import matplotlib.pyplot as plt
import numpy as np
import requests

resp = requests.get('http://192.168.1.3/result')
data = resp.content

table = np.array([int.from_bytes(data[i:i+4], 'little') for i in range(0, len(data), 4)])
table = table.reshape((-1, 5))

fig, ax = plt.subplots(1, 1, figsize=(16, 6))
ax.plot(table[:, 0], color='blue')
ax.plot(table[:, 1], color='red')
ax.plot(table[:, 2], color='green')
ax.plot(table[:, 3], color='orange')
ax.plot(table[:, 4], color='purple')
plt.show()