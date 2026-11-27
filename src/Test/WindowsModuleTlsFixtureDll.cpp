namespace
{
__declspec(thread) unsigned char TlsBytes[320] = {17};
}

extern "C" __declspec(dllexport) unsigned char *CubatariumFixtureTlsCell()
{
  return TlsBytes;
}
