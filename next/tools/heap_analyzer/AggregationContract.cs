namespace Wvd.HeapAnalyzer;

// Isolated projected observations exercise the same aggregation/report code.
// They are not ETL evidence and never set attribution_complete=true.
internal static class AggregationContract
{
    public static int Verify(string output)
    {
        var root=Path.GetFullPath(output);
        if(Directory.Exists(root)) throw new IOException("NEW_ISOLATED_OUTPUT_REQUIRED");
        Directory.CreateDirectory(root);
        var before=new Dictionary<string,HeapTotals>();long blocks=0,bytes=0;
        for(var i=0;i<100000;++i) HeapAggregation.AddAllocation(before,"unknown-stack",8,ref blocks,ref bytes);
        var row=before["unknown-stack"];
        HeapAggregation.AddAllocation(before,"unknown-stack",8,ref blocks,ref bytes);
        if(before.Count!=1 || !ReferenceEquals(row,before["unknown-stack"]) || bytes!=800008 || blocks!=100001)
            throw new InvalidDataException("UNKNOWN_BUCKET_OR_PER_ALLOCATION_OBJECT_GROWTH");
        HeapAggregation.AddAllocation(before,"known-A",4*1024*1024,ref blocks,ref bytes);
        var first=new SnapshotRows(1,1,"2026-01-01T00:00:00Z",false,"fixture",true,"fixture",blocks,bytes,800008,before);
        var after=new Dictionary<string,HeapTotals>();blocks=0;bytes=0;
        for(var i=0;i<100001;++i) HeapAggregation.AddAllocation(after,"unknown-stack",8,ref blocks,ref bytes);
        HeapAggregation.AddAllocation(after,"known-A",2*1024*1024,ref blocks,ref bytes);
        HeapAggregation.AddAllocation(after,"known-B",3*1024*1024,ref blocks,ref bytes);
        var second=new SnapshotRows(2,2,"2026-01-01T00:00:01Z",false,"fixture",true,"fixture",blocks,bytes,800008,after);
        var records=new Dictionary<string,StackRecord> {
            ["unknown-stack"]=new("unknown-stack","unknown-stack","unknown-stack",true,["unknown"]),
            ["known-A"]=new("known-A","fixture-module+1","fixture!AllocateA",false,[]),
            ["known-B"]=new("known-B","fixture-module+2","fixture!AllocateB",false,[])
        };
        var files=new ReportFiles(root,1024*1024);
        HeapAggregation.Write(files,[first,second],records);
        var deltas=File.ReadAllLines(Path.Combine(root,"heap-delta-by-stack.csv"));
        if(!deltas.Any(v=>v.Contains(",-2097152")) || !deltas.Any(v=>v.Contains(",3145728")))
            throw new InvalidDataException("POSITIVE_OR_NEGATIVE_DELTA_MISSING");
        var conservedRejected=false;
        Directory.CreateDirectory(Path.Combine(root,"corrupt"));
        try {HeapAggregation.Write(new ReportFiles(Path.Combine(root,"corrupt"),1024*1024),
            [first,second with {Bytes=second.Bytes+1}],records);}
        catch(InvalidDataException e) {conservedRejected=e.Message=="HEAP_DIFF_NOT_CONSERVED";}
        if(!conservedRejected) throw new InvalidDataException("CORRUPT_TOTALS_NOT_REJECTED");
        Directory.CreateDirectory(Path.Combine(root,"limited"));
        var limited=false;
        try {new ReportFiles(Path.Combine(root,"limited"),128).Json("report.json",new { data=new string('x',4096)});}
        catch(IOException e) {limited=e.Message=="ANALYZER_OUTPUT_BUDGET_EXCEEDED";}
        if(!limited || File.Exists(Path.Combine(root,"limited/report.json")))
            throw new InvalidDataException("OUTPUT_LIMIT_PUBLISHED_COMPLETE_REPORT");
        files.Json("fixture-result.json",new {source="isolated projected observations, NOT Windows ETL",passed=true,
            attribution_complete=false,known_positive_and_negative=true,unknown_bucket_bounded=true,
            conservation_fault_rejected=true,partial_output_preserved=true});
        Console.WriteLine("PASS production aggregate/report: stable unknown bucket, no per-allocation totals object, signed deltas, conservation and bounded partial output");
        return 0;
    }
}
