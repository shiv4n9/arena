import multiprocessing
import time

def stress():
    while True:
        pass

if __name__ == '__main__':
    print("Spiking CPU load on all cores for 30 seconds...")
    processes = []
    # Use max cores to ensure maximum scheduler disruption
    for _ in range(multiprocessing.cpu_count()):
        p = multiprocessing.Process(target=stress)
        p.start()
        processes.append(p)
    
    time.sleep(30)
    
    for p in processes:
        p.terminate()
    print("Stress test completed.")
