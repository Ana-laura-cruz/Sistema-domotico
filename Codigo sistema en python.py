import serial
import pandas as pd
import matplotlib.pyplot as plt
import os
import time  # <-- Importar para control de tiempo

# Crear directorio "Señales_EMG" si no existe
directorio_graficas = "Señales_EMG"
os.makedirs(directorio_graficas, exist_ok=True)

# Configurar puerto serial
puerto_com = "COM12"
baud_rate = 115200

# Inicializar la conexión serial
ser = serial.Serial(puerto_com, baud_rate)

print("Conectando con Arduino...")
ser.flushInput()
ser.flushOutput()

# Listas para almacenar los datos
tiempo = []
original = []
filtrada = []
rms = []

duracion_captura = 30  # segundos
inicio = time.time()

try:
    print("Leyendo datos del Arduino por 30 segundos...")
    while time.time() - inicio < duracion_captura:
        if ser.in_waiting > 0:
            linea = ser.readline().decode('utf-8').strip()
            valores = linea.split(",")

            if len(valores) == 4:
                tiempo.append(float(valores[0]))
                original.append(float(valores[1]))
                filtrada.append(float(valores[2]))
                rms.append(float(valores[3]))

    print("Tiempo de captura completado.")

except KeyboardInterrupt:
    print("\nLectura interrumpida por el usuario.")

finally:
    ser.close()
    print("Conexión serial cerrada.")

# Crear un DataFrame con los datos
datos = pd.DataFrame({
    "Tiempo": tiempo,
    "Original": original,
    "Filtrada": filtrada,
    "RMS": rms
})

# Guardar los datos en un archivo CSV
archivo_csv = "Resultados/Adquisicion_señal_EMG_Nivel_reposo _nuevo4_raw.csv"
os.makedirs("Resultados", exist_ok=True)  # Asegura que la carpeta exista
datos.to_csv(archivo_csv, index=False)
print(f"Datos guardados en: {archivo_csv}")

# Crear las gráficas
plt.figure(figsize=(12, 10))

plt.subplot(3, 1, 1)
plt.plot(tiempo, original, 'gray')
plt.title("Señal EMG Original")
plt.xlabel("Tiempo (ms)")
plt.ylabel("Amplitud")
plt.grid(True)

plt.subplot(3, 1, 2)
plt.plot(tiempo, filtrada, 'blue')
plt.title("Señal EMG Filtrada")
plt.xlabel("Tiempo (ms)")
plt.ylabel("Amplitud")
plt.grid(True)

plt.subplot(3, 1, 3)
plt.plot(tiempo, rms, 'red')
plt.title("RMS de Señal EMG")
plt.xlabel("Tiempo (ms)")
plt.ylabel("Amplitud RMS")
plt.grid(True)

plt.tight_layout()

nombre_archivo = f"{directorio_graficas}/Señal_EMG_{archivo_csv.split('/')[-1].split('.')[0]}.png"
plt.savefig(nombre_archivo, bbox_inches='tight', dpi=300)
plt.show()

print(f"Las gráficas se han guardado en: {nombre_archivo}")
