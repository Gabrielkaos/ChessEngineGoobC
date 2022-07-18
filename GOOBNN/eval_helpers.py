def eval_to_int(evaluation):
    try:
        res = int(evaluation)
    except ValueError:
        res = 10000
        eval=int(str(evaluation)[1:])
        # res*=int(str(evaluation)[1:])
        if eval<0:res*=-1
    return res

if __name__=="__main__":
    print(eval_to_int("-100"))