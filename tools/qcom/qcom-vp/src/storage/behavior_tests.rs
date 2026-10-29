use super::*;
use zip::write::SimpleFileOptions;

fn member(zip: &mut zip::ZipWriter<File>, name: &str, bytes: &[u8]) {
    zip.start_file(name, SimpleFileOptions::default()).unwrap();
    zip.write_all(bytes).unwrap();
}

fn gpt_pair(sectors: u64, name: &str) -> (Vec<u8>, Vec<u8>) {
    let mut entries = vec![0; 4096];
    entries[..16].fill(1);
    entries[16..32].fill(2);
    entries[32..40].copy_from_slice(&6u64.to_le_bytes());
    entries[40..48].copy_from_slice(&(sectors - 6).to_le_bytes());
    for (i, word) in name.encode_utf16().enumerate() {
        entries[56 + i * 2..58 + i * 2].copy_from_slice(&word.to_le_bytes());
    }
    let mut primary = vec![0; 4096 * 6];
    let mut backup = vec![0; 4096 * 5];
    primary[4096 * 2..4096 * 3].copy_from_slice(&entries);
    backup[..4096].copy_from_slice(&entries);
    for (target, at, other, array) in [
        (&mut primary[4096..8192], 1, sectors - 1, 2),
        (&mut backup[4096 * 4..4096 * 5], sectors - 1, 1, sectors - 5),
    ] {
        target[..8].copy_from_slice(b"EFI PART");
        target[8..12].copy_from_slice(&0x10000u32.to_le_bytes());
        target[12..16].copy_from_slice(&92u32.to_le_bytes());
        for (offset, value) in [
            (24, at),
            (32, other),
            (40, 6),
            (48, sectors - 6),
            (72, array),
        ] {
            target[offset..offset + 8].copy_from_slice(&value.to_le_bytes());
        }
        target[56..72].fill(3);
        target[80..84].copy_from_slice(&32u32.to_le_bytes());
        target[84..88].copy_from_slice(&128u32.to_le_bytes());
        target[88..92].copy_from_slice(&crc32fast::hash(&entries).to_le_bytes());
        let crc = crc32fast::hash(&target[..92]);
        target[16..20].copy_from_slice(&crc.to_le_bytes());
    }
    (primary, backup)
}

fn fixture(path: &Path, missing: bool) {
    let mut zip = zip::ZipWriter::new(File::create(path).unwrap());
    let mut provisioning = String::from("<data><ufs LUNtoGrow=\"0\" bBootEnable=\"1\"/>");
    let mut setting = String::from("<Setting>");
    for lun in 0..6 {
        provisioning.push_str(&format!("<ufs LUNum=\"{lun}\" bLUEnable=\"1\" size_in_kb=\"128\" bLogicalBlockSize=\"0x0c\" bBootLunID=\"{}\"/>", if lun == 1 || lun == 2 { lun } else {0}));
        setting.push_str(&format!("<Program{lun}><File Path=\"rawprogram{lun}.xml\"/></Program{lun}><Patch{lun}><File Path=\"patch{lun}.xml\"/></Patch{lun}>"));
        let mut program = String::from("<data>");
        for (filename, sector, count) in [
            (
                if missing && lun == 4 {
                    "sec.dat".to_owned()
                } else {
                    format!("data{lun}.bin")
                },
                "6",
                1,
            ),
            (format!("gpt_main{lun}.bin"), "0", 6),
            (format!("gpt_backup{lun}.bin"), "NUM_DISK_SECTORS-5.", 5),
        ] {
            let label = if filename == "sec.dat" {
                "secdata"
            } else {
                "payload"
            };
            program.push_str(&format!("<program filename=\"{filename}\" label=\"{label}\" start_sector=\"{sector}\" num_partition_sectors=\"{count}\" file_sector_offset=\"0\" SECTOR_SIZE_IN_BYTES=\"4096\" physical_partition_number=\"{lun}\" sparse=\"false\"/>"));
        }
        program.push_str("</data>");
        member(
            &mut zip,
            &format!("IMAGES/rawprogram{lun}.xml"),
            program.as_bytes(),
        );
        let mut patch = String::from("<patches>");
        for (filename, sector, value) in [
            (format!("data{lun}.bin"), 0, 9),
            ("DISK".to_owned(), 6, 9),
            ("DISK".to_owned(), 6, 7),
        ] {
            if missing && lun == 4 {
                continue;
            }
            patch.push_str(&format!("<patch filename=\"{filename}\" start_sector=\"{sector}\" byte_offset=\"1\" size_in_bytes=\"1\" value=\"{value}\" SECTOR_SIZE_IN_BYTES=\"4096\" physical_partition_number=\"{lun}\"/>"));
        }
        patch.push_str("</patches>");
        member(
            &mut zip,
            &format!("IMAGES/patch{lun}.xml"),
            patch.as_bytes(),
        );
        member(&mut zip, &format!("IMAGES/data{lun}.bin"), &[1, 2, 3, 4]);
        let (primary, backup) = gpt_pair(32, "boot_a");
        member(&mut zip, &format!("IMAGES/gpt_main{lun}.bin"), &primary);
        member(&mut zip, &format!("IMAGES/gpt_backup{lun}.bin"), &backup);
    }
    provisioning.push_str("</data>");
    setting.push_str("</Setting>");
    member(
        &mut zip,
        "IMAGES/provision_samsung.xml",
        provisioning.as_bytes(),
    );
    member(&mut zip, "IMAGES/Setting.xml", setting.as_bytes());
    member(
        &mut zip,
        "IMAGES/Projectconfig.xml",
        b"<data><program project=\"12345\"/></data>",
    );
    zip.finish().unwrap();
}

#[test]
fn assemble_six_luns_checks_sources_patches_gpt_and_report() {
    let directory = tempfile::tempdir().unwrap();
    let archive = directory.path().join("package.zip");
    fixture(&archive, false);
    let out = directory.path().join("prepared");
    std::fs::create_dir(&out).unwrap();
    let report = assemble(&archive, &out, false).unwrap();
    assert_eq!(report.luns.len(), 6);
    assert_eq!(report.consumed_members.len(), 33);
    for lun in report.luns {
        assert_eq!(lun.sectors, 32);
        assert_eq!(lun.partitions[0].name, "boot_a");
        let mut file = File::open(out.join(&lun.filename)).unwrap();
        file.seek(SeekFrom::Start(6 * 4096)).unwrap();
        let mut data = [0; 4];
        file.read_exact(&mut data).unwrap();
        assert_eq!(data, [1, 7, 3, 4]);
    }
    assert!(assemble(&archive, &out, false).is_err());
}

#[test]
fn missing_security_image_fails_before_creating_luns() {
    let directory = tempfile::tempdir().unwrap();
    let archive = directory.path().join("package.zip");
    fixture(&archive, true);
    let out = directory.path().join("prepared");
    std::fs::create_dir(&out).unwrap();
    assert!(assemble(&archive, &out, false).is_err());
    assert_eq!(std::fs::read_dir(out).unwrap().count(), 0);
}

#[test]
fn authorized_secdata_omission_preserves_other_writes() {
    let directory = tempfile::tempdir().unwrap();
    let archive = directory.path().join("package.zip");
    fixture(&archive, true);
    let out = directory.path().join("prepared");
    std::fs::create_dir(&out).unwrap();
    let report = assemble(&archive, &out, true).unwrap();
    assert_eq!(report.omitted_programs.len(), 1);
    assert_eq!(report.omitted_programs[0]["label"], "secdata");
    let mut empty = File::open(out.join("lun4.img")).unwrap();
    empty.seek(SeekFrom::Start(6 * 4096)).unwrap();
    let mut data = [1; 4096];
    empty.read_exact(&mut data).unwrap();
    assert_eq!(data, [0; 4096]);
    let mut populated = File::open(out.join("lun3.img")).unwrap();
    populated.seek(SeekFrom::Start(6 * 4096)).unwrap();
    populated.read_exact(&mut data[..4]).unwrap();
    assert_eq!(&data[..4], [1, 7, 3, 4]);
}

#[test]
fn gpt_rejects_each_crc_and_partition_bounds() {
    let mut file = tempfile::tempfile().unwrap();
    file.set_len(32 * 4096).unwrap();
    let (primary, backup) = gpt_pair(32, "boot_a");
    file.write_all(&primary).unwrap();
    file.seek(SeekFrom::Start(27 * 4096)).unwrap();
    file.write_all(&backup).unwrap();
    assert!(gpt::validate(&mut file, 4096, 32).is_ok());
    file.seek(SeekFrom::Start(31 * 4096 + 16)).unwrap();
    file.write_all(&[0; 4]).unwrap();
    assert!(gpt::validate(&mut file, 4096, 32).is_err());
    file.seek(SeekFrom::Start(27 * 4096)).unwrap();
    file.write_all(&backup).unwrap();
    file.seek(SeekFrom::Start(2 * 4096)).unwrap();
    file.write_all(&[9]).unwrap();
    assert!(gpt::validate(&mut file, 4096, 32).is_err());
}

#[test]
fn patch_crc_observes_prior_writes() {
    let mut file = tempfile::tempfile().unwrap();
    file.set_len(32 * 4096).unwrap();
    let lun = LunReport {
        lun: 0,
        filename: "unused".to_owned(),
        sector_size: 4096,
        sectors: 32,
        boot_lun_id: 0,
        partitions: Vec::new(),
    };
    let records: Vec<_> = [("0", "4", "0x12345678"), ("4", "4", "CRC32(0,4)")]
        .into_iter()
        .map(|(offset, width, value)| {
            [
                ("filename", "DISK"),
                ("physical_partition_number", "0"),
                ("SECTOR_SIZE_IN_BYTES", "4096"),
                ("byte_offset", offset),
                ("size_in_bytes", width),
                ("start_sector", "0"),
                ("value", value),
            ]
            .into_iter()
            .map(|(k, v)| (k.to_owned(), v.to_owned()))
            .collect()
        })
        .collect();
    apply_patches(&mut file, &lun, &records, &[]).unwrap();
    file.seek(SeekFrom::Start(4)).unwrap();
    let mut crc = [0; 4];
    file.read_exact(&mut crc).unwrap();
    assert_eq!(
        u32::from_le_bytes(crc),
        crc32fast::hash(&0x12345678u32.to_le_bytes())
    );
}

#[test]
fn mapped_extent_splits_at_fragment_boundaries() {
    let mut file = tempfile::tempfile().unwrap();
    file.set_len(64).unwrap();
    let mut sink = Sink::new(&mut file, 64, false, &[]);
    sink.mapping = vec![(0, 16, 3), (3, 32, 5)];
    sink.emit(0, &[1, 2, 3, 4, 5, 6, 7, 8]).unwrap();
    assert!(sink.emit(8, &[9]).is_err());
    let mut sink = Sink::new(&mut file, 64, true, &[]);
    sink.mapping = vec![(0, 16, 3), (3, 32, 5)];
    sink.emit(0, &[1, 2, 3, 4, 5, 6, 7, 8]).unwrap();
    assert!(sink.emit(0, &[9]).is_err());
}

#[test]
fn valid_crcs_do_not_allow_out_of_bounds_partitions() {
    let (mut primary, mut backup) = gpt_pair(32, "boot_a");
    primary[2 * 4096 + 40..2 * 4096 + 48].copy_from_slice(&32u64.to_le_bytes());
    backup[..4096].copy_from_slice(&primary[2 * 4096..3 * 4096]);
    let array_crc = crc32fast::hash(&backup[..4096]);
    for header in [&mut primary[4096..8192], &mut backup[4 * 4096..5 * 4096]] {
        header[88..92].copy_from_slice(&array_crc.to_le_bytes());
        header[16..20].fill(0);
        let crc = crc32fast::hash(&header[..92]);
        header[16..20].copy_from_slice(&crc.to_le_bytes());
    }
    let mut file = tempfile::tempfile().unwrap();
    file.set_len(32 * 4096).unwrap();
    file.write_all(&primary).unwrap();
    file.seek(SeekFrom::Start(27 * 4096)).unwrap();
    file.write_all(&backup).unwrap();
    assert!(gpt::validate(&mut file, 4096, 32).is_err());
}

#[test]
fn unsafe_archive_member_is_rejected_before_output() {
    let directory = tempfile::tempdir().unwrap();
    let archive = directory.path().join("unsafe.zip");
    let mut zip = zip::ZipWriter::new(File::create(&archive).unwrap());
    member(&mut zip, "../escape", b"x");
    zip.finish().unwrap();
    let out = directory.path().join("prepared");
    std::fs::create_dir(&out).unwrap();
    assert!(assemble(&archive, &out, false).is_err());
    assert_eq!(std::fs::read_dir(out).unwrap().count(), 0);
}

#[test]
fn unselected_links_do_not_block_regular_members() {
    let directory = tempfile::tempdir().unwrap();
    let archive = directory.path().join("links.zip");
    let mut zip = zip::ZipWriter::new(File::create(&archive).unwrap());
    member(&mut zip, "payload", b"firmware");
    zip.add_symlink("payload-link", "payload", SimpleFileOptions::default())
        .unwrap();
    zip.finish().unwrap();
    let mut package = Package::open(&archive).unwrap();
    assert_eq!(package.bytes("payload").unwrap(), b"firmware");
    assert!(package.bytes("payload-link").is_err());
}

#[test]
fn patch_ranges_widths_and_expressions_fail_closed() {
    let lun = LunReport {
        lun: 0,
        filename: "unused".to_owned(),
        sector_size: 4096,
        sectors: 32,
        boot_lun_id: 0,
        partitions: Vec::new(),
    };
    let base: Record = [
        ("filename", "DISK"),
        ("physical_partition_number", "0"),
        ("SECTOR_SIZE_IN_BYTES", "4096"),
        ("byte_offset", "0"),
        ("size_in_bytes", "4"),
        ("start_sector", "0"),
        ("value", "1"),
    ]
    .into_iter()
    .map(|(k, v)| (k.to_owned(), v.to_owned()))
    .collect();
    for (key, value) in [
        ("start_sector", "NUM_DISK_SECTORS"),
        ("size_in_bytes", "9"),
        ("value", "CRC32(0,18446744073709551615)"),
        ("value", "CRC32(1)"),
        ("value", "1/0"),
        ("physical_partition_number", "1"),
        ("SECTOR_SIZE_IN_BYTES", "512"),
    ] {
        let mut record = base.clone();
        record.insert(key.to_owned(), value.to_owned());
        assert!(patch_spec(&record, &lun, &[]).is_err(), "{key}={value}");
    }
    let mut file = tempfile::tempfile().unwrap();
    file.set_len(32 * 4096).unwrap();
    let mut record = base;
    record.insert("size_in_bytes".to_owned(), "1".to_owned());
    record.insert("value".to_owned(), "256".to_owned());
    assert!(apply_patches(&mut file, &lun, &[record], &[]).is_err());
}

fn uart_lun(blocks: u64) -> (File, LunReport) {
    let sectors = blocks + 11;
    let mut file = tempfile::tempfile().unwrap();
    file.set_len(sectors * 4096).unwrap();
    let (primary, backup) = gpt_pair(sectors, OPLUS_UART_PARTITION);
    file.write_all(&primary).unwrap();
    file.seek(SeekFrom::Start((sectors - 5) * 4096)).unwrap();
    file.write_all(&backup).unwrap();
    let partitions = gpt::validate(&mut file, 4096, sectors).unwrap();
    let lun = LunReport {
        lun: 5,
        filename: "lun5.img".to_owned(),
        sector_size: 4096,
        sectors,
        boot_lun_id: 0,
        partitions,
    };
    (file, lun)
}

fn file_digest(file: &mut File) -> [u8; 32] {
    file.rewind().unwrap();
    let mut digest = Sha256::new();
    let mut buffer = [0; 4096];
    loop {
        let count = file.read(&mut buffer).unwrap();
        if count == 0 {
            return digest.finalize().into();
        }
        digest.update(&buffer[..count]);
    }
}

#[test]
fn oem_uart_uses_the_partition_tail_and_preserves_neighboring_records() {
    let (mut file, lun) = uart_lun(3072);
    let record_lba = 2095;
    let window_start = (record_lba - 1) * 4096;
    let mut window = [0x63; 3 * 4096];
    file.seek(SeekFrom::Start(window_start)).unwrap();
    file.write_all(&window).unwrap();
    let record = enable_oplus_uart(&mut file, &lun).unwrap();
    assert_eq!(record.record_lba, record_lba);
    file.seek(SeekFrom::Start(window_start)).unwrap();
    file.read_exact(&mut window).unwrap();
    assert_eq!(&window[4096..4112], b"ENABLE_UART:TRUE");
    assert!(window[..4096].iter().all(|byte| *byte == 0x63));
    assert!(window[4112..].iter().all(|byte| *byte == 0x63));
    assert_eq!(
        gpt::validate(&mut file, 4096, lun.sectors).unwrap(),
        lun.partitions
    );
}

#[test]
fn oem_uart_rejects_a_partition_smaller_than_the_reserved_tail() {
    let (mut file, lun) = uart_lun(0x7ff);
    let before = file_digest(&mut file);
    assert!(enable_oplus_uart(&mut file, &lun).is_err());
    assert_eq!(file_digest(&mut file), before);
}

#[test]
fn oem_uart_rejects_stale_partition_identity_before_writing() {
    let (mut file, mut lun) = uart_lun(0x800);
    lun.partitions[0].last_lba -= 1;
    let before = file_digest(&mut file);
    assert!(enable_oplus_uart(&mut file, &lun).is_err());
    assert_eq!(file_digest(&mut file), before);
}
