import librosa
import librosa.display
import matplotlib.pyplot as plt
import numpy as np

# 1. Chargement des pistes (Assurez-vous d'utiliser vos vrais chemins)
frelon, sr = librosa.load("C:/Users/simon/Desktop/Esiee/projet fin d'année/fichier sonore frelon asiatique/frelon2.wav", sr=22050)
avion, _ = librosa.load("C:/Users/simon/Desktop/Esiee/projet fin d'année/fichier sonore frelon asiatique/bruit decollage avion.wav", sr=22050)

# 2. Extraction des caractéristiques
# CORRECTION : On analyse bien le fichier 'avion' pour les MFCC de l'avion
mfcc_avion = librosa.feature.mfcc(y=avion, sr=sr, n_mfcc=13)
mfcc_frelon = librosa.feature.mfcc(y=frelon, sr=sr, n_mfcc=13)

# Calcul du spectrogramme (STFT) pour le frelon
stft_frelon = librosa.stft(frelon, n_fft=8192, hop_length=1024)
# CORRECTION : On passe l'amplitude en décibels pour un affichage lisible
stft_frelon_db = librosa.amplitude_to_db(np.abs(stft_frelon), ref=np.max)

# 3. Affichage : Création de 3 sous-graphiques (3 lignes, 1 colonne)
fig, ax = plt.subplots(3, 1, figsize=(10, 12))

# --- Graphique 1 : MFCC Avion ---
img1 = librosa.display.specshow(mfcc_avion, x_axis='time', sr=sr, cmap='coolwarm', ax=ax[0])
ax[0].set_title('MFCC - Bruit de décollage (Avion)')
ax[0].set_ylabel('Coefs (1 à 13)')
fig.colorbar(img1, ax=ax[0], format="%+2.0f")

# --- Graphique 2 : MFCC Frelon ---
img2 = librosa.display.specshow(mfcc_frelon, x_axis='time', sr=sr, cmap='coolwarm', ax=ax[1])
ax[1].set_title('Empreinte MFCC du bourdonnement duFrelon Asiatique')
ax[1].set_ylabel('Coefs (1 à 13)')
fig.colorbar(img2, ax=ax[1], format="%+2.0f")

# --- Graphique 3 : Spectrogramme STFT Frelon ---
# Utilisation de y_axis='log' car le battement d'ailes a des fréquences spécifiques intéressantes en échelle log
img3 = librosa.display.specshow(stft_frelon_db, x_axis='time', y_axis='log', sr=sr, cmap='magma', ax=ax[2])
ax[2].set_title('Spectrogramme bourdonnement du Frelon Asiatique (TF)')
ax[2].set_ylabel('Fréquence (Hz)')
ax[2].set_xlabel('Temps (s)')
fig.colorbar(img3, ax=ax[2], format="%+2.0f dB")

# Mise en page aérée
plt.tight_layout(pad=3.0)
plt.show()