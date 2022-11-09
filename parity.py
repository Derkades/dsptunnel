
def parity(inp):
    p = 0
    q = 0
    for i, b in enumerate(inp):
        p ^= b
        q ^= ((1 << i) * b) % 256
    return p, q

inp = b'test'
p, q = parity(inp)

print('p', p)
print('q', q)

# print(chr(inp[1] ^ p))
# print(ord('t'))
# print(ord('e'))
print(chr(parity(b'\0est')[0] ^ p))
print(chr(parity(b't\0st')[0] ^ p))
print(chr(parity(b'te\0t')[0] ^ p))
print(chr(parity(b'tes\0')[0] ^ p))
print(chr((parity(b'\0est')[1] ^ q)))
print(chr((parity(b't\0st')[1] ^ q) >> 1))
print(chr((parity(b'te\0t')[1] ^ q) >> 2))
print(chr((parity(b'tes\0')[1] ^ q) >> 3))