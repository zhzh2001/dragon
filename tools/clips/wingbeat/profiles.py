"""Read a model's profile or the flight model's default."""
import re


def profile_path(root, model, suffix):
    filename = f'{model}.glb.{suffix}.cfg'
    beside_model = root / 'assets' / filename
    return beside_model if beside_model.is_file() else root / 'assets/species' / filename


def flight_value(root, model, name):
    path = profile_path(root, model, 'flight')
    for line in path.read_text().splitlines():
        fields = line.partition('#')[0].split()
        if fields and fields[0] == name:
            return float(fields[1])
    header = (root / 'src/game/flight.h').read_text()
    return float(re.search(rf'float {re.escape(name)} = ([-.\d]+)f;', header)[1])
