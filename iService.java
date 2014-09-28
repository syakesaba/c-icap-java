public interface iService {
	public void initialize(final String mod_type, final String[] headers);
	public int preview(final byte[] data, final String[] headers);
	public byte[] service(final byte[] body, String[] headers);
}
/*
  <init>();
    descriptor: ()V

  public void initialize(java.lang.String, java.lang.String[]);
    descriptor: (Ljava/lang/String;[Ljava/lang/String;)V

  public int preview(byte[], java.lang.String[]);
    descriptor: ([B[Ljava/lang/String;)I

  public byte[] service(byte[], java.lang.String[]);
    descriptor: ([B[Ljava/lang/String;)[B
*/