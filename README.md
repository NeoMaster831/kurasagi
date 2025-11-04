# kurasagi

`kurasagi` is full POC of PatchGuard bypass for Windows 24H2 - 25H2.

For more information, please refer to the `product` branch, which contains the PDF paper detailing the bypass.

**If any BSOD which is related to `CRITICAL_STRUCTURE_CORRUPTION` (PatchGuard) appears, please create issue with it!**

## Disclaimers

1. **PLEASE USE IT FOR ONLY EDUCATIONAL PURPOSES!**
2. Do not turn on hypervisor-based security factors when running! (It will BSOD!)
3. Use [kdmapper](https://github.com/TheCruZ/kdmapper) for driver loading.
4. After `kurasagi` has been loaded, we just found there's some weird issue when you allocate pool with `NonPagedPoolExecute` (or `NonPagedPool`, it is same), it is not executable. I'll fix as soon as possible.

# Images

![proof](assets/proof.png)

## Credit
Here are the helpful resources I referred to in completing this project. I appreciate these works, ideas, and source codes. Thanks
+ https://blog.tetrane.com/downloads/Tetrane_PatchGuard_Analysis_RS4_v1.01.pdf
+ https://blog.can.ac/2024/06/28/pgc-garbage-collecting-patchguard/
+ https://shhoya.github.io/windows_pgintro.html
