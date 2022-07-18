
import random

def eval(skill):
    val=100
    return (skill * val / 100 + (100 - skill) * random.randint(0,100) / 100)


print(eval(100))