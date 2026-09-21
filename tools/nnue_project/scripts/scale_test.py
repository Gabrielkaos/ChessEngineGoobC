import torch
ckpt = torch.load("../checkpoints/nnue.pt", map_location="cpu")
sd = ckpt["model_state_dict"]

def suggest(name, bits, margin=0.9):
    limit = 2 ** (bits - 1) - 1
    max_abs = sd[name].abs().max().item()
    return int(margin * limit / max_abs)

print("qa (ft.weight, int16):", suggest("ft.weight", 16))
print("qb (fc2.weight, int8):", suggest("fc2.weight", 8))
print("qc (fc3.weight, int8):", suggest("fc3.weight", 8))
print("qd (fc4.weight, int8):", suggest("fc4.weight", 8))