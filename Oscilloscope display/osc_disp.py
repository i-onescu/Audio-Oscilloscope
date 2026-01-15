import serial
import numpy as np
import matplotlib.pyplot as plt
import tkinter as tk
from matplotlib.backends.backend_tkagg import FigureCanvasTkAgg


PORT = "COM10"
BAUDRATE = 1152000
SAMPLES = 992
PACKET_CONTENT = 31
VREF = 3.3
FRAME_STEP = 32
ADC_RATE = 1000000
FFT_MAX_FREQ = 100000
TOP_FREQ_COUNT = 8  # how many dominant frequencies to display


ser = serial.Serial(PORT, BAUDRATE, timeout=1)


def read_frame():
    while True:
        if ser.read(1) == b'\xAA':
            if ser.read(1) == b'\x55':
                data = ser.read(PACKET_CONTENT * 2)
                if len(data) == PACKET_CONTENT * 2:
                    return data

def read_992_samples():
    adc_buffer = bytearray()
    for _ in range(32):
        frame = read_frame()
        adc_buffer.extend(frame)
    return np.frombuffer(adc_buffer, dtype=np.uint16)


root = tk.Tk()
root.title("Oscilloscop")
fig_wave, ax_wave = plt.subplots(figsize=(8,4))
voltage_buffer = np.zeros(SAMPLES)
line_wave, = ax_wave.plot(voltage_buffer)
ax_wave.set_ylim(0, VREF)
ax_wave.set_xlim(0, SAMPLES)
ax_wave.set_ylabel("Volts (V)")
ax_wave.set_xlabel("Samples")
canvas_wave = FigureCanvasTkAgg(fig_wave, master=root)
canvas_wave.get_tk_widget().pack(side=tk.TOP, fill=tk.BOTH, expand=1)

# Fourier
fig_fft, ax_fft = plt.subplots(figsize=(8,3))
line_fft, = ax_fft.plot(np.zeros(SAMPLES//2))
ax_fft.set_xlim(0, FFT_MAX_FREQ)
ax_fft.set_ylim(0, 1)
ax_fft.set_xlabel("Frequency (Hz)")
ax_fft.set_ylabel("Magnitude")
canvas_fft = FigureCanvasTkAgg(fig_fft, master=root)
canvas_fft.get_tk_widget().pack(side=tk.TOP, fill=tk.BOTH, expand=1)

# First 8 dominan freq
# freq_frame = tk.Frame(root)
# freq_frame.pack(side=tk.TOP, pady=5)
# freq_labels = []
# for i in range(TOP_FREQ_COUNT):
#     lbl = tk.Label(freq_frame, text=f"Freq {i+1}: --- Hz")
#     lbl.pack()
#     freq_labels.append(lbl)

# Capture button
capturing = tk.BooleanVar(value=True)
def toggle_capture():
    capturing.set(not capturing.get())
    btn.config(text="Stop Capture" if capturing.get() else "Start Capture")

btn = tk.Button(root, text="Stop Capture", command=toggle_capture)
btn.pack(pady=5)


def update_plot():
    global voltage_buffer
    if capturing.get():
        samples = read_992_samples()
        voltage = samples * VREF / 4095

        # Left-to-right printing
        n = min(FRAME_STEP*32, len(voltage))
        voltage_buffer[:-n] = voltage_buffer[n:]
        voltage_buffer[-n:] = voltage[:n]

        line_wave.set_ydata(voltage_buffer)
        canvas_wave.draw()

        # FFT 
        fft_result = np.fft.fft(voltage_buffer)
        fft_magnitude = np.abs(fft_result) / len(voltage_buffer)
        freqs = np.fft.fftfreq(len(voltage_buffer), d=1/ADC_RATE)
        pos_mask = (freqs >= 0) & (freqs <= FFT_MAX_FREQ)
        line_fft.set_ydata(fft_magnitude[pos_mask])
        line_fft.set_xdata(freqs[pos_mask])
        canvas_fft.draw()

        # Dominant frequencies
        # pos_freqs = freqs[pos_mask]
        # pos_mags = fft_magnitude[pos_mask]
        # top_indices = np.argsort(pos_mags)[-TOP_FREQ_COUNT:][::-1]  # descending
        # for i, idx in enumerate(top_indices):
        #     freq_labels[i].config(text=f"Freq {i+1}: {pos_freqs[idx]:.1f} Hz ({pos_mags[idx]:.3f})")

    root.after(10, update_plot)

update_plot()
root.mainloop()
ser.close()
