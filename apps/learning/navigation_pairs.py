def build_pairs(segment):
    result = []
    n = len(segment)
    for i in range(n - 1):
        start = segment[i]
        best = None
        best_time = 0
        best_dist = 0
        for j in range(i + 1, min(i + 21, n)):
            end = segment[j]
            time_diff = (end['time_ms'] - start['time_ms']) / 1000
            if time_diff > 1:
                break
            dist = sum((a - b)**2 for a, b in zip(start['position'][:3], end['position'][:3]))**0.5
            best = end
            best_time = time_diff
            best_dist = dist
            if dist >= 2:
                break
        if best is not None:
            time_diff = best_time
            dist = best_dist
            if 0.05 <= time_diff <= 1 and 0.5 <= dist <= 10:
                pos = start['position'].copy()
                vel = [(best['position'][k] - start['position'][k]) / time_diff for k in range(3)]
                result.append((pos, vel))
    return result
