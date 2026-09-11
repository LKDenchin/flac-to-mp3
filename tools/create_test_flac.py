#!/usr/bin/env python3
import os
import sys
import subprocess

def create_cover_image(path):
    # Generate a simple 100x100 Red JPEG image using python PIL or ffmpeg raw picture
    try:
        from PIL import Image
        img = Image.new('RGB', (100, 100), color=(255, 64, 64))
        img.save(path)
    except ImportError:
        # Fallback to ffmpeg
        subprocess.run([
            'ffmpeg', '-y', '-f', 'lavfi', '-i', 'color=c=red:s=100x100:d=1',
            '-vframes', '1', path
        ], check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

def generate_flac(output_dir):
    os.makedirs(output_dir, exist_ok=True)
    cover_path = os.path.join(output_dir, "cover.jpg")
    create_cover_image(cover_path)

    songs = [
        {"file": "song1.flac", "title": "Summer Dreams", "artist": "Ocean Waves", "album": "Breeze", "year": "2024", "track": "1"},
        {"file": "song2.flac", "title": "Midnight City", "artist": "Nightfall", "album": "Neon Lights", "year": "2023", "track": "2"},
        {"file": "nested/song3.flac", "title": "Starlight Serenade", "artist": "Luna", "album": "Cosmos", "year": "2025", "track": "3"}
    ]

    for song in songs:
        file_path = os.path.join(output_dir, song["file"])
        os.makedirs(os.path.dirname(file_path), exist_ok=True)

        # Generate 2 seconds of 440Hz sine wave audio encoded in FLAC with metadata & cover art
        cmd = [
            'ffmpeg', '-y',
            '-f', 'lavfi', '-i', 'sine=frequency=440:duration=2',
            '-i', cover_path,
            '-map', '0:a', '-map', '1:v',
            '-c:a', 'flac',
            '-c:v', 'mjpeg',
            '-disposition:v', 'attached_pic',
            '-metadata', f'title={song["title"]}',
            '-metadata', f'artist={song["artist"]}',
            '-metadata', f'album={song["album"]}',
            '-metadata', f'date={song["year"]}',
            '-metadata', f'track={song["track"]}',
            file_path
        ]
        subprocess.run(cmd, check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        print(f"Generated test FLAC: {file_path}")

    # Generate a fake invalid file to test magic number check
    fake_path = os.path.join(output_dir, "fake_renamed.flac")
    with open(fake_path, "w") as f:
        f.write("This is not a real flac file!")
    print(f"Generated fake invalid file: {fake_path}")

if __name__ == "__main__":
    out = sys.argv[1] if len(sys.argv) > 1 else "/tmp/flac_test_in"
    generate_flac(out)
