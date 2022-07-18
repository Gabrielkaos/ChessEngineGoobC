import numpy as np



n_list=np.array([i for i in range(100000)])
np.random.shuffle(n_list)

def find(n,n_list):

    #bubble sort



    for i in range(len(n_list)-1-1):
        if (i % 100)==0:
            print(f"[PROCESSING] Iter:{i}")
        for j in range(len(n_list)):
            if n_list[i]+n_list[j]==n:
                return i,j


    return None,None


i,j=find(100001,n_list)

print(f"Index1 {i} -> {n_list[i]}")
print(f"Index2 {j} -> {n_list[j]}")
